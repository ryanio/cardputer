#include <ArduinoJson.h>

#include "../net.h"
#include "../store.h"
#include "../ui.h"
#include "../view.h"

// What a transaction costs right now, and whether that is cheap for today.
//
// The tip leads, the way it does on gwei.ryanio.com. The base fee is the
// network's price and everybody in the block pays the same one, so the tip is
// the only figure anybody actually chooses: it is the number you came for and
// the number you type into a wallet. The base fee sits under it as context.
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

enum class Screen : uint8_t { Now, Alarm };
enum class Job : uint8_t { None, Gas, History };

Screen screen = Screen::Now;
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

void drawSparkline(int top, int height)
{
	M5GFX &g = ui::gfx();
	if (pointCount < 2) {
		ui::small(3, top + height / 2 - 4, "no history yet", ui::RULE);
		return;
	}

	// Log, because gas moves multiplicatively. A day that spends twenty hours
	// near 0.04 and spikes once to 0.75 is a flat line along the floor on a
	// linear axis: the spike owns the whole height and the shape everybody
	// actually reads is gone. The printed low and high stay linear.
	float lo = logf(max(gwei[0], 1e-6f));
	float hi = lo;
	for (int i = 1; i < pointCount; i++) {
		const float v = logf(max(gwei[i], 1e-6f));
		lo = min(lo, v);
		hi = max(hi, v);
	}
	if (hi - lo < 1e-4f) {
		hi = lo + 1e-4f;
	}

	// Three columns short of the edge, so the dot on the newest point has room
	// to be a dot rather than half of one.
	const uint32_t span = age[pointCount - 1] == 0 ? 1 : age[pointCount - 1];
	auto xAt = [&](int i) { return (int)((float)age[i] / (float)span * (ui::W - 4)); };
	auto yAt = [&](float v) {
		const float l = logf(max(v, 1e-6f));
		return top + height - 1 - (int)((l - lo) / (hi - lo) * (float)(height - 1));
	};

	// The threshold sits behind the line, so a glance says how far off it is.
	if (armed && alarm >= lo && alarm <= hi) {
		const int y = yAt(alarm);
		for (int x = 0; x < ui::W; x += 4) {
			g.drawPixel(x, y, ui::RULE);
		}
	}

	for (int i = 1; i < pointCount; i++) {
		g.drawLine(xAt(i - 1), yAt(gwei[i - 1]), xAt(i), yAt(gwei[i]), band().color);
	}
	g.fillCircle(xAt(pointCount - 1), yAt(gwei[pointCount - 1]), 2, ui::FG);
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
		gweiText(low24, number, sizeof(number));
		ui::small(3, 114, number, ui::DIM);
		gweiText(high24, number, sizeof(number));
		smallRight(237, 114, number, ui::DIM);
		snprintf(text, sizeof(text), "%s%s, %d%% of 24h", band().name, armed ? " armed" : "", p);
		smallCentre(114, text, band().color);
	} else {
		snprintf(text, sizeof(text), "block %ld", block);
		smallCentre(114, text, ui::RULE);
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
	ui::small(3, 64, "nothing here runs in the background.", ui::DIM);

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
	ui::small(3, 110, "enter arms, del erases, 0 is off", ui::DIM);
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

	if (screen == Screen::Alarm) {
		drawAlarm();
	} else {
		drawNow();
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
	screen = Screen::Now;
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
			screen = Screen::Now;
		}
	} else if (k.del) {
		if (length == 0) {
			screen = Screen::Now;
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
		// The tip is the number worth picking, so the arrows pick it.
		if (tierCount > 0) {
			speed = k.up ? (speed + tierCount - 1) % tierCount : (speed + 1) % tierCount;
			store::setInt(SPEED_KEY, speed);
		}
	} else if (k.ch == 'a' || k.ch == 'A' || k.enter || k.right) {
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
