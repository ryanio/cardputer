#include <ArduinoJson.h>

#include "../net.h"
#include "../store.h"
#include "../ui.h"
#include "../view.h"

// What a transaction costs right now, and whether that is cheap for today.
//
// Three pages, because there are three questions and one screen cannot hold
// them: what is it now, what has it been today, and what is a normal hour
// worth. Left and right page between them, up and down pick the speed, and the
// speed follows you across all three, which is what makes it one app rather
// than three screens that happen to share a fetch.
//
// The tip leads, the way it does on gwei.ryanio.com. The base fee is the
// network's price and everybody in the block pays the same one, so the tip is
// the only figure anybody actually chooses: it is the number you came for and
// the number you type into a wallet. The base fee sits under it as context.
//
// Every history point carries the tip as well as the base fee, which is the
// whole reason there is more than one graph here: the two move together for
// most of a day and then come apart, and the tip is the half you choose.
//
// This is the view the network stack was built for: the smallest payload of
// the five, fetched on the tightest loop, so if HTTPS and JSON work anywhere
// they work here. It is also the only view that polls, and it polls at exactly
// the rate the source refreshes: gwei recomputes its snapshot at most every
// 30s and does it lazily on request, so asking faster returns the same bytes
// and costs somebody else's compute for nothing.
//
// An absolute threshold for congestion would be a lie a month from now, since
// base fees have moved by two orders of magnitude inside a year. The band comes
// from where the current fee sits in the day's own 24 hours instead, which is
// the question somebody actually has: is now a good time.
namespace {

constexpr const char *GAS_URL = "https://gwei.ryanio.com/api/gas";
constexpr const char *HISTORY_URL = "https://gwei.ryanio.com/api/gas/history";

constexpr const char *ALARM_KEY = "gas.alarm";
constexpr const char *ARMED_KEY = "gas.armed";
constexpr const char *SPEED_KEY = "gas.speed";
constexpr const char *PAGE_KEY = "gas.page";

constexpr uint32_t POLL_MS = 30000;         // the source's own window
constexpr uint32_t HISTORY_MS = 5 * 60000;  // the shape of a day moves slowly
constexpr int MAX_POINTS = 240;             // one per column, and no more use for any
constexpr int TIERS = 3;
constexpr size_t ENTRY_MAX = 10;

// The names come from the roadmap. The boundaries come from the day, because
// the API publishes no bands and inventing four constants would date badly.
struct Band {
	const char *name;
	uint16_t color;
	int upTo;  // percentile of the last 24 hours, inclusive
};
constexpr Band BANDS[] = {
    {"Chill", ui::GOOD, 25},
    {"Busy", ui::WARN, 50},
    {"Chaos", ui::CORAL, 75},
    {"Whale", ui::BAD, 100},
};

struct Tier {
	char label[10];
	char eta[10];
	float tip;  // the priority fee, which is the part you pick
	float total;
	float usd;
	bool hasUsd;
};

enum class Screen : uint8_t { Now, Day, Hours, Alarm };
constexpr int PAGES = 3;  // the alarm is a mode you enter, not a page you land on
constexpr int BUCKETS = 24;
enum class Job : uint8_t { None, Gas, History };

Screen screen = Screen::Now;
Screen back = Screen::Now;  // the page the alarm was opened from
Job job = Job::None;
bool waitingShown = false;
int status = 0;

bool haveGas = false;
bool haveHistory = false;
uint32_t gasAt = 0;
uint32_t historyAt = 0;

float baseFee = 0.0f;
float ethPrice = 0.0f;
bool hasPrice = false;
long block = 0;
Tier tiers[TIERS];
int tierCount = 0;
// The source sends fast, normal, cheap in that order. Normal is the one most
// people want and the one the site opens on.
int speed = 1;

float gwei[MAX_POINTS];
float tipAt[MAX_POINTS];   // the median tip at that moment, which the history carries
uint32_t age[MAX_POINTS];  // seconds after the first point, so the x axis is time
int pointCount = 0;
float low24 = 0.0f;
float high24 = 0.0f;

float alarm = 0.0f;
bool armed = false;
bool sounded = false;  // true while the fee is under the threshold, so it fires once
char entry[ENTRY_MAX + 1] = {0};

// ------------------------------------------------------------------ helpers

// Base fees here run from four hundredths of a gwei to a few gwei, so the
// number of useful digits moves with the value.
void gweiText(float v, char *out, size_t n)
{
	if (v >= 100.0f) {
		snprintf(out, n, "%.0f", v);
	} else if (v >= 10.0f) {
		snprintf(out, n, "%.1f", v);
	} else if (v >= 1.0f) {
		snprintf(out, n, "%.2f", v);
	} else {
		snprintf(out, n, "%.3f", v);
	}
}

void usdText(float v, char *out, size_t n)
{
	if (v >= 1.0f) {
		snprintf(out, n, "$%.2f", v);
	} else {
		snprintf(out, n, "%.1fc", v * 100.0f);
	}
}

// Where the current fee sits among the last 24 hours, as a percentile. The
// distribution is heavily skewed, so quartiles of the raw range would call
// almost everything cheap: counting points is the honest version.
int percentile()
{
	if (pointCount == 0) {
		return -1;
	}
	int below = 0;
	for (int i = 0; i < pointCount; i++) {
		if (gwei[i] < baseFee) {
			below++;
		}
	}
	return below * 100 / pointCount;
}

const Band &band()
{
	const int p = percentile();
	for (const Band &b : BANDS) {
		if (p <= b.upTo) {
			return b;
		}
	}
	return BANDS[0];
}

// Right aligned Font0, which is six pixels a character, so this is arithmetic.
void smallRight(int right, int y, const char *text, uint16_t color)
{
	ui::small(right - (int)strlen(text) * 6, y, text, color);
}

// --------------------------------------------------------------------- fetch

void want(Job next)
{
	job = next;
	waitingShown = false;
	view::repaint();
}

void fetchGas()
{
	JsonDocument filter;
	filter["baseFeeGwei"] = true;
	filter["ethPriceUsd"] = true;
	filter["blockNumber"] = true;
	JsonObject option = filter["speeds"][0].to<JsonObject>();
	option["label"] = true;
	option["eta"] = true;
	option["priorityFeeGwei"] = true;
	option["totalGwei"] = true;
	option["usdPerTransfer"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(GAS_URL, doc, &filter);
	status = r.status;
	if (!r.ok()) {
		return;
	}

	baseFee = doc["baseFeeGwei"] | 0.0f;
	block = doc["blockNumber"] | 0L;
	// A missing price is its own state and never a zero: the fee is still worth
	// showing, it just cannot be priced.
	hasPrice = !doc["ethPriceUsd"].isNull();
	ethPrice = hasPrice ? doc["ethPriceUsd"].as<float>() : 0.0f;

	tierCount = 0;
	for (JsonObjectConst s : doc["speeds"].as<JsonArrayConst>()) {
		if (tierCount >= TIERS) {
			break;
		}
		Tier &t = tiers[tierCount];
		ui::asciify(s["label"] | "", t.label, sizeof(t.label));
		ui::asciify(s["eta"] | "", t.eta, sizeof(t.eta));
		t.tip = s["priorityFeeGwei"] | 0.0f;
		t.total = s["totalGwei"] | 0.0f;
		t.hasUsd = !s["usdPerTransfer"].isNull();
		t.usd = t.hasUsd ? s["usdPerTransfer"].as<float>() : 0.0f;
		tierCount++;
	}

	if (speed >= tierCount) {
		speed = tierCount > 1 ? 1 : 0;
	}
	haveGas = true;
	gasAt = millis();
}

void fetchHistory()
{
	JsonDocument filter;
	JsonObject point = filter["points"][0].to<JsonObject>();
	point["t"] = true;
	point["gwei"] = true;
	point["tip"] = true;
	filter["low24h"] = true;
	filter["high24h"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(HISTORY_URL, doc, &filter);
	status = r.status;
	if (!r.ok()) {
		return;
	}

	JsonArrayConst points = doc["points"];
	const int total = (int)points.size();
	// More points than the panel has columns, so the oldest fall off the left
	// rather than being averaged into something that never happened.
	const int skip = total > MAX_POINTS ? total - MAX_POINTS : 0;

	pointCount = 0;
	int64_t first = 0;
	int index = 0;
	for (JsonObjectConst p : points) {
		if (index++ < skip) {
			continue;
		}
		const int64_t t = p["t"] | (int64_t)0;
		if (pointCount == 0) {
			first = t;
		}
		age[pointCount] = (uint32_t)((t - first) / 1000);
		gwei[pointCount] = p["gwei"] | 0.0f;
		// Points written before the tip was recorded are still worth charting,
		// so a missing one is zero and the tip line simply does not start there.
		tipAt[pointCount] = p["tip"] | 0.0f;
		pointCount++;
	}

	low24 = doc["low24h"] | 0.0f;
	high24 = doc["high24h"] | 0.0f;
	haveHistory = pointCount > 0;
	historyAt = millis();
}

// The alarm watches while this screen is open, which is the honest scope: no
// view runs in the background, and a poller that did would hammer a source
// that refreshes twice a minute.
void checkAlarm()
{
	if (!armed || !haveGas || alarm <= 0.0f) {
		return;
	}
	if (baseFee > alarm) {
		sounded = false;  // armed again as soon as it climbs back over
		return;
	}
	if (sounded) {
		return;
	}
	sounded = true;

	// A real speaker, so a rising arpeggio rather than a beep.
	M5Cardputer.Speaker.tone(523.0f, 90);
	delay(100);
	M5Cardputer.Speaker.tone(659.0f, 90);
	delay(100);
	M5Cardputer.Speaker.tone(784.0f, 160);
	view::note("gas is under your number");
}

// ---------------------------------------------------------------------- draw

// Log, because gas moves multiplicatively. A day that spends twenty hours near
// 0.04 and spikes once to 0.75 is a flat line along the floor on a linear axis:
// the spike owns the whole height and the shape everybody actually reads is
// gone. Printed numbers stay linear.
float logOf(float v)
{
	return logf(max(v, 1e-6f));
}

// The window both series have to share, so the tip is drawn against the base
// fee rather than against its own range, which is the only way the gap between
// them means anything.
void series(bool withTip, float &lo, float &hi)
{
	lo = logOf(gwei[0]);
	hi = lo;
	for (int i = 0; i < pointCount; i++) {
		const float v = logOf(gwei[i]);
		lo = min(lo, v);
		hi = max(hi, v);
		if (withTip && tipAt[i] > 0.0f) {
			const float t = logOf(tipAt[i]);
			lo = min(lo, t);
			hi = max(hi, t);
		}
	}
	if (hi - lo < 1e-4f) {
		hi = lo + 1e-4f;
	}
}

// One series as a line. The x axis is time rather than sample number, because
// the history arrives every five minutes on a good hour and every fifteen on a
// bad one, and drawing those the same width would lie about when things moved.
void plot(const float *values, int top, int height, float lo, float hi, uint16_t color, bool dot)
{
	M5GFX &g = ui::gfx();
	const uint32_t span = age[pointCount - 1] == 0 ? 1 : age[pointCount - 1];
	auto xAt = [&](int i) { return (int)((float)age[i] / (float)span * (ui::W - 4)); };
	auto yAt = [&](float v) {
		return top + height - 1 - (int)((logOf(v) - lo) / (hi - lo) * (float)(height - 1));
	};

	int last = -1;
	for (int i = 0; i < pointCount; i++) {
		if (values[i] <= 0.0f) {
			continue;  // before this series was recorded, so it starts later
		}
		if (last >= 0) {
			g.drawLine(xAt(last), yAt(values[last]), xAt(i), yAt(values[i]), color);
		}
		last = i;
	}
	if (dot && last >= 0) {
		g.fillCircle(xAt(last), yAt(values[last]), 2, ui::FG);
	}
}

// The threshold behind the line, so a glance says how far off it is. It is
// compared in log space because that is the space the chart is drawn in.
void plotAlarm(int top, int height, float lo, float hi)
{
	if (!armed || alarm <= 0.0f) {
		return;
	}
	const float l = logOf(alarm);
	if (l < lo || l > hi) {
		return;
	}
	const int y = top + height - 1 - (int)((l - lo) / (hi - lo) * (float)(height - 1));
	for (int x = 0; x < ui::W; x += 4) {
		ui::gfx().drawPixel(x, y, ui::RULE);
	}
}

// The line under the headline is the tip, so the numbers at its ends are the
// tip's own day. low24h and high24h from the source are the base fee alone and
// would label this line with a range it never touches.
void tipRange(float &lo, float &hi)
{
	lo = 0.0f;
	hi = 0.0f;
	for (int i = 0; i < pointCount; i++) {
		if (tipAt[i] <= 0.0f) {
			continue;
		}
		if (lo == 0.0f || tipAt[i] < lo) {
			lo = tipAt[i];
		}
		if (tipAt[i] > hi) {
			hi = tipAt[i];
		}
	}
}

void drawSparkline(int top, int height)
{
	if (pointCount < 2) {
		ui::small(3, top + height / 2 - 4, "no history yet", ui::RULE);
		return;
	}
	float lo = 0.0f;
	float hi = 0.0f;
	// The page above it leads with the tip, so the line under it is the tip.
	series(true, lo, hi);
	plotAlarm(top, height, lo, hi);
	plot(tipAt, top, height, lo, hi, band().color, true);
}

void smallCentre(int y, const char *text, uint16_t color)
{
	ui::small(ui::W / 2 - (int)strlen(text) * 3, y, text, color);
}

// The three speeds on one row, because the headline already shows the one that
// is picked and this row is for comparing and switching. Up and down move it.
void drawSpeeds(int y)
{
	M5GFX &g = ui::gfx();
	char number[16];
	g.fillRect(0, y - 2, ui::W, 12, ui::BG);

	const int cell = ui::W / (tierCount > 0 ? tierCount : 1);
	for (int i = 0; i < tierCount; i++) {
		char text[24];
		gweiText(tiers[i].tip, number, sizeof(number));
		snprintf(text, sizeof(text), "%s %s", tiers[i].label, number);
		const int width = (int)strlen(text) * 6;
		const int x = i * cell + (cell - width) / 2;
		if (i == speed) {
			g.fillRoundRect(x - 4, y - 2, width + 8, 12, 3, ui::PANEL);
		}
		ui::small(x, y, text, i == speed ? band().color : ui::DIM);
	}
}

void drawNow()
{
	char text[48];
	char number[16];
	char tip[16];
	char total[16];

	ui::clearBody();

	// The tip is the headline. Everybody in the block pays the same base fee,
	// so this is the only figure on the screen anybody gets to choose.
	const Tier &t = tiers[speed < tierCount ? speed : 0];
	gweiText(t.tip, tip, sizeof(tip));
	ui::bigNumber(tip, band().color, "gwei tip", 12);

	// What it is a tip for, and what it costs, on one row.
	snprintf(text, sizeof(text), "%s, %s", t.label, t.eta);
	ui::small(3, 60, text, ui::DIM);
	if (t.hasUsd) {
		usdText(t.usd, number, sizeof(number));
		snprintf(text, sizeof(text), "%s to send eth", number);
	} else {
		snprintf(text, sizeof(text), "%s", "no eth price");
	}
	smallRight(237, 60, text, ui::DIM);

	// The sum, which is the part a wallet asks for and the part the base fee
	// explains. Nothing else on the screen says what you actually pay.
	gweiText(baseFee, number, sizeof(number));
	gweiText(t.total, total, sizeof(total));
	snprintf(text, sizeof(text), "%s base + %s tip = %s total", number, tip, total);
	smallCentre(72, text, ui::FG);

	drawSpeeds(83);

	ui::gfx().drawFastHLine(0, 96, ui::W, ui::RULE);
	drawSparkline(99, 14);

	const int p = percentile();
	if (haveHistory) {
		float tipLow = 0.0f;
		float tipHigh = 0.0f;
		tipRange(tipLow, tipHigh);
		gweiText(tipLow, number, sizeof(number));
		ui::small(3, 114, number, ui::DIM);
		gweiText(tipHigh, number, sizeof(number));
		smallRight(237, 114, number, ui::DIM);
		snprintf(text, sizeof(text), "%s%s, %d%% of 24h", band().name, armed ? " armed" : "", p);
		smallCentre(114, text, band().color);
	} else {
		snprintf(text, sizeof(text), "block %ld", block);
		smallCentre(114, text, ui::RULE);
	}
}

// ------------------------------------------------------------------ page two
//
// Both series over the day, on one axis, which is the point: for most of a day
// the tip is a tenth of the base fee and tracks it, and the hours where it
// comes loose are the hours worth knowing about.
void drawDay()
{
	char text[48];
	char number[16];
	M5GFX &g = ui::gfx();

	ui::clearBody();
	ui::small(3, 4, "the last 24 hours", ui::FG);

	if (pointCount < 2) {
		ui::small(3, 56, "no history yet", ui::RULE);
		return;
	}

	float lo = 0.0f;
	float hi = 0.0f;
	series(true, lo, hi);

	constexpr int TOP = 26;
	constexpr int TALL = 74;
	// The axis is labelled at both ends of the drawn range rather than with the
	// day's published low and high, because those are the base fee alone and
	// this chart has the tip in it too.
	gweiText(expf(hi), number, sizeof(number));
	smallRight(237, TOP - 10, number, ui::RULE);
	gweiText(expf(lo), number, sizeof(number));
	smallRight(237, TOP + TALL + 2, number, ui::RULE);

	g.drawFastHLine(0, TOP - 1, ui::W, ui::RULE);
	g.drawFastHLine(0, TOP + TALL, ui::W, ui::RULE);
	plotAlarm(TOP, TALL, lo, hi);
	plot(gwei, TOP, TALL, lo, hi, ui::DIM, false);
	plot(tipAt, TOP, TALL, lo, hi, band().color, true);

	// A legend, and the two numbers the lines end on.
	gweiText(baseFee, number, sizeof(number));
	snprintf(text, sizeof(text), "base %s", number);
	g.fillRect(3, 17, 8, 2, ui::DIM);
	ui::small(14, 14, text, ui::DIM);

	const Tier &t = tiers[speed < tierCount ? speed : 0];
	gweiText(t.tip, number, sizeof(number));
	snprintf(text, sizeof(text), "tip %s", number);
	g.fillRect(123, 17, 8, 2, band().color);
	ui::small(134, 14, text, band().color);

	ui::small(3, 112, "24h ago", ui::RULE);
	const int p = percentile();
	snprintf(text, sizeof(text), "%s, %d%% of the day", band().name, p);
	smallCentre(112, text, band().color);
	smallRight(237, 112, "now", ui::RULE);
}

// ---------------------------------------------------------------- page three
//
// The same day as twenty four bars, which is the shape a line at this width
// cannot show: whether the quiet hours are a run or a scatter, and how far the
// cheap end really is from the dear one.
float medianOf(float *values, int count)
{
	// Insertion sort. A bucket holds a handful of points and this runs once a
	// repaint, so the simple one is the right one.
	for (int i = 1; i < count; i++) {
		const float key = values[i];
		int j = i - 1;
		while (j >= 0 && values[j] > key) {
			values[j + 1] = values[j];
			j--;
		}
		values[j + 1] = key;
	}
	return count % 2 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2.0f;
}

void drawHours()
{
	char text[48];
	char number[16];
	M5GFX &g = ui::gfx();

	ui::clearBody();
	ui::small(3, 4, "the day, hour by hour", ui::FG);

	if (pointCount < 2) {
		ui::small(3, 56, "no history yet", ui::RULE);
		return;
	}

	// Hours back from the newest point, not clock hours: nothing on the device
	// knows what time it is anywhere, and "six hours ago" is the question
	// somebody standing here actually has.
	const uint32_t newest = age[pointCount - 1];
	float bar[BUCKETS];
	int cheapest = -1;
	int dearest = -1;
	for (int b = 0; b < BUCKETS; b++) {
		float samples[40];
		int count = 0;
		for (int i = 0; i < pointCount && count < (int)(sizeof(samples) / sizeof(samples[0]));
		     i++) {
			const uint32_t back = newest - age[i];
			const int at = BUCKETS - 1 - (int)(back / 3600);
			if (at == b) {
				// What it would have cost, which is the pair added up rather
				// than either half of it.
				samples[count++] = gwei[i] + tipAt[i];
			}
		}
		bar[b] = count > 0 ? medianOf(samples, count) : 0.0f;
		if (bar[b] > 0.0f) {
			if (cheapest < 0 || bar[b] < bar[cheapest]) {
				cheapest = b;
			}
			if (dearest < 0 || bar[b] > bar[dearest]) {
				dearest = b;
			}
		}
	}
	if (cheapest < 0) {
		ui::small(3, 56, "not enough of the day yet", ui::RULE);
		return;
	}

	constexpr int FLOOR = 100;
	constexpr int TALL = 72;
	const float lo = logOf(bar[cheapest]);
	const float hi = logOf(bar[dearest]);
	const float range = hi - lo < 1e-4f ? 1e-4f : hi - lo;
	const int wide = ui::W / BUCKETS;  // ten columns each, which is the panel exactly

	for (int b = 0; b < BUCKETS; b++) {
		if (bar[b] <= 0.0f) {
			continue;
		}
		// A floor of three rows, so an hour that happened reads as a bar and
		// not as an hour with no data in it.
		const int high = 3 + (int)((logOf(bar[b]) - lo) / range * (float)(TALL - 3));
		const uint16_t color = b == BUCKETS - 1 ? ui::FG
		                       : b == cheapest  ? ui::GOOD
		                       : b == dearest   ? ui::BAD
		                                        : ui::RULE;
		g.fillRect(b * wide + 1, FLOOR - high, wide - 2, high, color);
	}
	g.drawFastHLine(0, FLOOR, ui::W, ui::RULE);

	gweiText(bar[dearest], number, sizeof(number));
	snprintf(text, sizeof(text), "dearest %s", number);
	ui::small(3, 14, text, ui::BAD);
	gweiText(bar[cheapest], number, sizeof(number));
	snprintf(text, sizeof(text), "cheapest %s", number);
	smallRight(237, 14, text, ui::GOOD);

	ui::small(3, 104, "24h ago", ui::RULE);
	smallRight(237, 104, "now", ui::RULE);
	const int ago = BUCKETS - 1 - cheapest;
	if (ago == 0) {
		snprintf(text, sizeof(text), "%s", "this hour is the cheapest so far");
	} else {
		snprintf(text, sizeof(text), "cheapest %dh ago, bars are medians", ago);
	}
	ui::small(3, 114, text, ui::DIM);
}

// Which of the three you are on, in the corner every page leaves empty.
void drawDots()
{
	M5GFX &g = ui::gfx();
	const int here = screen == Screen::Now ? 0 : screen == Screen::Day ? 1 : 2;
	for (int i = 0; i < PAGES; i++) {
		g.fillCircle(ui::W - 26 + i * 9, 6, i == here ? 2 : 1, i == here ? ui::FG : ui::RULE);
	}
}

void drawAlarm()
{
	char text[48];
	char number[16];

	ui::clearBody();
	ui::title("Alarm", ui::CORAL);

	if (entry[0] != '\0') {
		snprintf(text, sizeof(text), "under %s gwei_", entry);
	} else if (armed) {
		gweiText(alarm, number, sizeof(number));
		snprintf(text, sizeof(text), "sounds under %s gwei", number);
	} else {
		snprintf(text, sizeof(text), "off, type a number");
	}
	ui::line(0, text, armed || entry[0] != '\0' ? ui::FG : ui::DIM);

	ui::small(3, 54, "it watches while this screen is open.", ui::DIM);
	// Picking a threshold needs to know what the day has been, and this is the
	// base fee's own published range rather than anything drawn on a chart.
	if (haveHistory && high24 > 0.0f) {
		char lowText[16];
		gweiText(low24, lowText, sizeof(lowText));
		gweiText(high24, number, sizeof(number));
		snprintf(text, sizeof(text), "the base fee ran %s to %s today", lowText, number);
	} else {
		snprintf(text, sizeof(text), "%s", "nothing here runs in the background.");
	}
	ui::small(3, 64, text, ui::DIM);

	ui::gfx().drawFastHLine(0, 78, ui::W, ui::RULE);
	snprintf(text, sizeof(text), "block %ld", block);
	ui::small(3, 84, text, ui::DIM);
	if (hasPrice) {
		snprintf(text, sizeof(text), "eth $%.0f", ethPrice);
	} else {
		snprintf(text, sizeof(text), "no eth price");
	}
	smallRight(237, 84, text, ui::DIM);

	if (haveGas) {
		snprintf(text, sizeof(text), "read %us ago, every %us", (millis() - gasAt) / 1000,
		         POLL_MS / 1000);
	} else {
		snprintf(text, sizeof(text), "%s", net::statusText(status));
	}
	ui::small(3, 96, text, ui::DIM);
	ui::small(3, 110, "enter arms, del goes back, 0 is off", ui::DIM);
}

void draw()
{
	if (!haveGas) {
		if (job != Job::None) {
			ui::message("gas prices", "the smallest payload of the five");
			ui::spinner(ui::W / 2, 96);
		} else {
			ui::message("no gas", net::online() ? net::statusText(status) : "needs wifi", ui::WARN);
		}
		waitingShown = true;
		return;
	}

	switch (screen) {
		case Screen::Alarm:
			drawAlarm();
			break;
		case Screen::Day:
			drawDay();
			drawDots();
			break;
		case Screen::Hours:
			drawHours();
			drawDots();
			break;
		default:
			drawNow();
			drawDots();
			break;
	}
	if (job != Job::None) {
		// A refresh repaints in place rather than blanking the screen: there is
		// already a number on it, and it is still the right one.
		waitingShown = true;
	}
}

// ---------------------------------------------------------------- lifecycle

void enter()
{
	const int page = constrain(store::getInt(PAGE_KEY, 0), 0, PAGES - 1);
	screen = page == 0 ? Screen::Now : page == 1 ? Screen::Day : Screen::Hours;
	back = screen;
	entry[0] = '\0';
	alarm = store::getFloat(ALARM_KEY, 0.0f);
	armed = store::getBool(ARMED_KEY, false);
	speed = constrain(store::getInt(SPEED_KEY, 1), 0, TIERS - 1);
	sounded = false;
	M5Cardputer.Speaker.begin();
	M5Cardputer.Speaker.setVolume(110);
	if (net::online() && !haveGas) {
		want(Job::Gas);
	}
}

void leave()
{
	job = Job::None;
	M5Cardputer.Speaker.stop();
}

void tick()
{
	if (job != Job::None) {
		if (!waitingShown) {
			return;
		}
		const Job running = job;
		job = Job::None;
		waitingShown = false;
		if (running == Job::Gas) {
			fetchGas();
			checkAlarm();
			if (haveGas && !haveHistory) {
				want(Job::History);
			}
		} else {
			fetchHistory();
		}
		view::repaint();
		return;
	}

	if (!net::online()) {
		return;
	}
	// The one poll in the firmware, at the rate the source refreshes.
	if (!haveGas || millis() - gasAt > POLL_MS) {
		want(Job::Gas);
	} else if (haveGas && millis() - historyAt > HISTORY_MS) {
		want(Job::History);
	}
}

// ---------------------------------------------------------------------- keys

bool alarmKey(const view::Key &k)
{
	const size_t length = strlen(entry);
	if (k.enter) {
		if (length > 0) {
			alarm = atof(entry);
			armed = alarm > 0.0f;
			sounded = false;
			store::setFloat(ALARM_KEY, alarm);
			store::setBool(ARMED_KEY, armed);
			entry[0] = '\0';
			view::note(armed ? "armed" : "alarm off");
			// Arming under a fee that is already low should sound now, not in
			// thirty seconds when the next poll happens to come round.
			checkAlarm();
		} else {
			screen = back;
		}
	} else if (k.del) {
		if (length == 0) {
			screen = back;
		} else {
			entry[length - 1] = '\0';
		}
	} else if ((isdigit((unsigned char)k.ch) || k.ch == '.') && length < ENTRY_MAX) {
		// The four arrow keys print their own characters and the down arrow is
		// the full stop, which a threshold under one gwei needs.
		entry[length] = k.ch;
		entry[length + 1] = '\0';
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool key(const view::Key &k)
{
	if (screen == Screen::Alarm) {
		return alarmKey(k);
	}

	if (k.up || k.down) {
		// The tip is the number worth picking, so the arrows pick it, and the
		// pick follows you onto the other two pages.
		if (tierCount > 0) {
			speed = k.up ? (speed + tierCount - 1) % tierCount : (speed + 1) % tierCount;
			store::setInt(SPEED_KEY, speed);
		}
	} else if (k.left || k.right) {
		const int here = screen == Screen::Now ? 0 : screen == Screen::Day ? 1 : 2;
		const int next = (here + (k.right ? 1 : PAGES - 1)) % PAGES;
		screen = next == 0 ? Screen::Now : next == 1 ? Screen::Day : Screen::Hours;
		store::setInt(PAGE_KEY, next);
	} else if (k.ch == 'a' || k.ch == 'A' || k.enter) {
		back = screen;
		screen = Screen::Alarm;
		entry[0] = '\0';
	} else if (k.ch == 'r' || k.ch == 'R') {
		if (net::online()) {
			haveHistory = false;  // both halves, since this is somebody asking
			want(Job::Gas);
		} else {
			view::note("no wifi");
		}
		return true;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

const view::View kGas = {
    .name = "Gas",
    .source = "GWEI",
    .order = view::ORDER_GAS,
    .icon = icons::FUEL,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kGas);
