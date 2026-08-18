#include <ArduinoJson.h>

#include "../net.h"
#include "../ui.h"
#include "../view.h"

// Bankr says which agents are earning. Coral says what its own read of the
// agent's token is. Crossing the two is the whole point of this view: the
// leaderboard is a public unauthenticated fetch, every profile carries a token
// address, and that address is exactly what Coral's score endpoint takes.
//
// Three screens, each one a level deeper: the leaderboard, one agent, and the
// Coral score of that agent's token. del backs out a level and the backtick
// leaves altogether, which the view loop handles before this file sees a key.
//
// Nothing here polls. The leaderboard is fetched once and kept, and a score is
// a full lookup that took seconds every time it was probed, so it happens when
// somebody asks for it and never on a timer.
namespace {

constexpr const char *PROFILES_URL = "https://api.bankr.bot/agent-profiles?limit=20";
constexpr const char *SCORE_URL = "https://api.0xcoral.com/api/v1/score/%s/%s";

constexpr int MAX_AGENTS = 20;
constexpr int LIST_ROWS = 7;  // row 0 says what the ranking is
constexpr int MAX_BULLETS = 5;
constexpr uint32_t STALE_MS = 10 * 60 * 1000;

struct Agent {
	char name[40];
	char symbol[16];
	char address[45];
	char chain[12];
	float mcap;
	float volume;
	float revenue;  // weekly, in WETH. The API sends it as a decimal string.
	int products;
	bool hasToken;
};

struct Score {
	char address[45];
	int value;
	float confidence;
	char verdict[16];
	char confidenceLabel[12];
	char headline[128];
	char bullets[MAX_BULLETS][64];
	int bulletCount;
	char caveat[128];
};

enum class Screen : uint8_t { List, Detail, Score };
enum class State : uint8_t { Empty, Waiting, Ready, Failed };
enum class Job : uint8_t { None, Profiles, Scoring };

Screen screen = Screen::List;
State listState = State::Empty;
State scoreState = State::Empty;

// A fetch blocks for as long as the request takes, so it cannot start in the
// same pass that asks for it: the waiting screen would never reach the panel.
// draw() sets waitingShown once it has painted, and tick() goes only then.
Job job = Job::None;
bool waitingShown = false;
int listStatus = 0;
int scoreStatus = 0;

Agent agents[MAX_AGENTS];
int agentCount = 0;
int total = 0;
uint32_t fetchedAt = 0;

int pick = 0;
int pickTop = 0;
int page = 0;  // the score screen: 0 is the read, 1 is why

Score score;

// ------------------------------------------------------------------ helpers

// The panel's fonts are ASCII. Coral writes its bullets with a middle dot and
// occasionally a curly quote, which arrive as UTF-8 and would otherwise draw as
// rubble, so anything above ASCII is folded down or dropped.
void asciify(const char *src, char *out, size_t n)
{
	size_t at = 0;
	while (*src != '\0' && at + 1 < n) {
		const uint8_t c = (uint8_t)*src;
		if (c < 0x80) {
			out[at++] = *src++;
			continue;
		}
		const uint8_t next = (uint8_t)src[1];
		if (c == 0xC2 && next == 0xB7) {  // a middle dot separates the two halves
			out[at++] = '|';
			src += 2;
			continue;
		}
		if (c == 0xE2 && next == 0x80) {
			const uint8_t last = (uint8_t)src[2];
			if (last == 0x99 || last == 0x98) {
				out[at++] = '\'';
			} else if (last == 0x9C || last == 0x9D) {
				out[at++] = '"';
			}
			src += 3;
			continue;
		}
		// Some other multibyte run: step over its continuation bytes.
		src++;
		while (((uint8_t)*src & 0xC0) == 0x80) {
			src++;
		}
	}
	out[at] = '\0';
}

void copyField(JsonVariantConst value, char *out, size_t n, const char *fallback = "")
{
	asciify(value.is<const char *>() ? value.as<const char *>() : fallback, out, n);
}

// Dollars in the width a 240px row can spare.
void usd(float v, char *out, size_t n)
{
	if (v <= 0) {
		snprintf(out, n, "unknown");
	} else if (v >= 1e9f) {
		snprintf(out, n, "$%.2fB", v / 1e9f);
	} else if (v >= 1e6f) {
		snprintf(out, n, "$%.2fM", v / 1e6f);
	} else if (v >= 1e3f) {
		snprintf(out, n, "$%.1fK", v / 1e3f);
	} else {
		snprintf(out, n, "$%.0f", v);
	}
}

// Weekly revenue runs from 12 WETH down to two digits after the point, so the
// small numbers keep four places and the big ones keep two.
void weth(float v, char *out, size_t n)
{
	snprintf(out, n, v >= 1.0f ? "%.2f WETH" : "%.4f WETH", v);
}

// Trim to a pixel budget in Font2, so a long agent name cannot run underneath
// the market cap sitting on its right.
void fit(const char *text, int budget, char *out, size_t n)
{
	M5GFX &g = ui::gfx();
	g.setFont(&fonts::Font2);
	snprintf(out, n, "%s", text);
	while (out[0] != '\0' && g.textWidth(out) > budget) {
		out[strlen(out) - 1] = '\0';
	}
}

// Font0 is fixed at six pixels a character, which is what makes a caveat or a
// headline fit at all. Wrapping is on whitespace, and a word longer than the
// line is left to run off rather than being broken.
int wrapSmall(const char *text, int chars, char lines[][44], int maxLines)
{
	int count = 0;
	int at = 0;
	const int len = (int)strlen(text);
	while (at < len && count < maxLines) {
		int take = len - at;
		if (take > chars) {
			take = chars;
			int space = take;
			while (space > 0 && text[at + space] != ' ') {
				space--;
			}
			if (space > 0) {
				take = space;
			}
		}
		if (take > 43) {
			take = 43;
		}
		memcpy(lines[count], text + at, take);
		lines[count][take] = '\0';
		count++;
		at += take;
		while (at < len && text[at] == ' ') {
			at++;
		}
	}
	return count;
}

// Font0 draws six pixels a character, so what fits is arithmetic. Anything
// past the right edge is cut and marked, the way the rest of the UI cuts.
void smallText(int y, const char *text, uint16_t color, int x = 3)
{
	const int room = (ui::W - 3 - x) / 6;
	if (room <= 0) {
		return;
	}
	char cut[48];
	if ((int)strlen(text) > room) {
		const int keep = room - 1 < (int)sizeof(cut) - 1 ? room - 1 : (int)sizeof(cut) - 2;
		memcpy(cut, text, keep);
		cut[keep] = '.';
		cut[keep + 1] = '\0';
		text = cut;
	}

	M5GFX &g = ui::gfx();
	g.setFont(&fonts::Font0);
	g.setTextColor(color, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString(text, x, y);
}

uint16_t verdictColor(const char *verdict)
{
	if (strcmp(verdict, "organic") == 0) {
		return ui::GOOD;
	}
	if (strcmp(verdict, "manufactured") == 0 || strcmp(verdict, "suspicious") == 0) {
		return ui::BAD;
	}
	return ui::WARN;  // unknown, and anything Coral adds later
}

// --------------------------------------------------------------------- fetch

void want(Job next)
{
	job = next;
	waitingShown = false;
	if (next == Job::Profiles) {
		listState = State::Waiting;
	} else if (next == Job::Scoring) {
		scoreState = State::Waiting;
	}
	view::repaint();
}

void fetchProfiles()
{
	JsonDocument filter;
	JsonObject wanted = filter["profiles"][0].to<JsonObject>();
	wanted["projectName"] = true;
	wanted["tokenSymbol"] = true;
	wanted["tokenAddress"] = true;
	wanted["tokenChainId"] = true;
	wanted["marketCapUsd"] = true;
	wanted["vol24hUsd"] = true;
	wanted["weeklyRevenueWeth"] = true;
	wanted["productsCount"] = true;
	filter["total"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(PROFILES_URL, doc, &filter);
	listStatus = r.status;
	if (!r.ok()) {
		listState = State::Failed;
		return;
	}

	agentCount = 0;
	total = doc["total"] | 0;
	for (JsonObjectConst p : doc["profiles"].as<JsonArrayConst>()) {
		if (agentCount >= MAX_AGENTS) {
			break;
		}
		Agent &a = agents[agentCount];
		copyField(p["projectName"], a.name, sizeof(a.name), "unnamed");
		copyField(p["tokenSymbol"], a.symbol, sizeof(a.symbol));
		copyField(p["tokenAddress"], a.address, sizeof(a.address));
		copyField(p["tokenChainId"], a.chain, sizeof(a.chain));
		a.mcap = p["marketCapUsd"] | 0.0f;
		a.volume = p["vol24hUsd"] | 0.0f;
		// The API sends weekly revenue as a decimal string, not a number.
		a.revenue = (float)atof(p["weeklyRevenueWeth"] | "0");
		a.products = p["productsCount"] | 0;
		// One profile of the 113 carries no token at all. It still earns and
		// still belongs on the leaderboard, it just has nothing to score.
		a.hasToken = a.address[0] == '0' && a.chain[0] != '\0';
		agentCount++;
	}

	listState = agentCount > 0 ? State::Ready : State::Failed;
	fetchedAt = millis();
	if (pick >= agentCount) {
		pick = 0;
		pickTop = 0;
	}
}

void fetchScore()
{
	const Agent &a = agents[pick];
	char url[128];
	snprintf(url, sizeof(url), SCORE_URL, a.chain, a.address);

	JsonDocument filter;
	filter["score"] = true;
	filter["verdict"] = true;
	filter["confidence"] = true;
	filter["confidenceLabel"] = true;
	JsonObject explanation = filter["explanation"].to<JsonObject>();
	explanation["headline"] = true;
	explanation["bullets"] = true;
	explanation["caveats"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	scoreStatus = r.status;
	if (!r.ok()) {
		scoreState = State::Failed;
		return;
	}

	snprintf(score.address, sizeof(score.address), "%s", a.address);
	score.value = doc["score"] | 0;
	score.confidence = doc["confidence"] | 0.0f;
	copyField(doc["verdict"], score.verdict, sizeof(score.verdict), "unknown");
	copyField(doc["confidenceLabel"], score.confidenceLabel, sizeof(score.confidenceLabel), "");
	copyField(doc["explanation"]["headline"], score.headline, sizeof(score.headline));

	score.bulletCount = 0;
	for (JsonVariantConst b : doc["explanation"]["bullets"].as<JsonArrayConst>()) {
		if (score.bulletCount >= MAX_BULLETS) {
			break;
		}
		copyField(b, score.bullets[score.bulletCount], sizeof(score.bullets[0]));
		score.bulletCount++;
	}

	// Coral sends its caveats as an array and the API repeats them in a header.
	// They are not decoration, so one of them is on screen behind every score.
	JsonArrayConst caveats = doc["explanation"]["caveats"].as<JsonArrayConst>();
	copyField(caveats.isNull() ? JsonVariantConst() : caveats[0], score.caveat,
	          sizeof(score.caveat), "Not a price target, audit, or trading recommendation.");

	scoreState = State::Ready;
}

// ---------------------------------------------------------------------- draw

void drawList()
{
	char text[64];
	char name[48];

	if (listState == State::Waiting) {
		ui::message("reading the leaderboard", "bankr agent profiles");
		ui::spinner(ui::W / 2, 92);
		waitingShown = true;
		return;
	}
	if (listState == State::Failed) {
		ui::message("no leaderboard", net::statusText(listStatus), ui::WARN);
		ui::lineAt(105, "r tries again", ui::DIM, textdatum_t::top_center);
		return;
	}
	if (listState == State::Empty) {
		ui::message("bankr", net::online() ? "r loads the leaderboard" : "needs wifi");
		return;
	}

	ui::clearBody();
	snprintf(text, sizeof(text), "%d agents, by market cap", total);
	ui::line(0, text, ui::DIM);

	if (pick < pickTop) {
		pickTop = pick;
	} else if (pick >= pickTop + LIST_ROWS) {
		pickTop = pick - LIST_ROWS + 1;
	}

	for (int row = 0; row < LIST_ROWS; row++) {
		const int i = pickTop + row;
		if (i >= agentCount) {
			break;
		}
		const Agent &a = agents[i];
		const bool on = i == pick;

		usd(a.mcap, text, sizeof(text));
		const int y = ui::contentTop() + (row + 1) * ui::LINE_H;

		// The market cap owns the right of the row, so the name is cut to
		// whatever is left rather than sliding underneath it.
		M5GFX &g = ui::gfx();
		g.setFont(&fonts::Font2);
		const int reserved = (int)g.textWidth(text) + 10;
		char label[56];
		snprintf(label, sizeof(label), "%s%d %s", on ? "> " : "  ", i + 1, a.name);
		fit(label, ui::W - 6 - reserved, name, sizeof(name));

		ui::line(row + 1, name, on ? ui::CORAL : ui::FG);
		ui::lineAt(y, text, on ? ui::CORAL : ui::DIM, textdatum_t::top_right);
	}
}

void drawDetail()
{
	char text[64];
	const Agent &a = agents[pick];

	ui::clearBody();
	ui::title(a.name);

	snprintf(text, sizeof(text), "%s on %s", a.symbol[0] == '\0' ? "no token" : a.symbol,
	         a.chain[0] == '\0' ? "no chain" : a.chain);
	ui::line(0, text, ui::DIM);

	const char *labels[] = {"market cap", "24h volume", "weekly rev", "products"};
	char values[4][24];
	usd(a.mcap, values[0], sizeof(values[0]));
	usd(a.volume, values[1], sizeof(values[1]));
	weth(a.revenue, values[2], sizeof(values[2]));
	snprintf(values[3], sizeof(values[3]), "%d", a.products);

	for (int i = 0; i < 4; i++) {
		ui::line(i + 1, labels[i], ui::DIM);
		ui::lineAt(ui::contentTop() + (i + 1) * ui::LINE_H, values[i], ui::FG,
		           textdatum_t::top_right);
	}

	if (a.hasToken) {
		ui::line(6, "enter asks Coral about it", ui::CORAL);
	} else {
		ui::line(6, "no token, so nothing to score", ui::DIM);
	}
}

// One line of a Coral bullet. They arrive as a fact and a reading of it split
// by a middle dot, so the fact keeps full contrast and the reading steps back.
void drawBullet(int y, const char *bullet)
{
	const char *split = strchr(bullet, '|');
	if (split == nullptr) {
		smallText(y, bullet, ui::FG);
		return;
	}
	char head[64];
	size_t n = (size_t)(split - bullet);
	if (n >= sizeof(head)) {
		n = sizeof(head) - 1;
	}
	memcpy(head, bullet, n);
	head[n] = '\0';
	smallText(y, head, ui::FG);
	smallText(y, split + 1, ui::DIM, 3 + (int)n * 6);
}

// Both score pages end with a caveat. CLAUDE.md makes that a rule rather than
// a nicety: a number this confident looking does not go on screen alone.
void drawCaveat()
{
	char lines[2][44];
	const int count = wrapSmall(score.caveat, 39, lines, 2);
	ui::gfx().drawFastHLine(0, 96, ui::W, ui::RULE);
	for (int i = 0; i < count; i++) {
		smallText(101 + i * 10, lines[i], ui::DIM);
	}
}

void drawScore()
{
	char text[64];
	const Agent &a = agents[pick];

	if (scoreState == State::Waiting) {
		ui::message("asking Coral", "a full lookup takes a few seconds", ui::CORAL);
		ui::spinner(ui::W / 2, 96);
		waitingShown = true;
		return;
	}
	if (scoreState == State::Failed) {
		ui::message("Coral had no answer", net::statusText(scoreStatus), ui::WARN);
		ui::lineAt(105, "enter asks again", ui::DIM, textdatum_t::top_center);
		return;
	}

	ui::clearBody();

	if (page == 0) {
		snprintf(text, sizeof(text), "Coral on %s", a.symbol[0] == '\0' ? a.name : a.symbol);
		ui::title(text, ui::CORAL);

		snprintf(text, sizeof(text), "%d", score.value);
		ui::bigNumber(text, verdictColor(score.verdict), score.verdict, ui::TITLE_H + 2);

		snprintf(text, sizeof(text), "confidence %s, %.2f", score.confidenceLabel,
		         score.confidence);
		smallText(64, text, ui::DIM);

		char lines[2][44];
		const int count = wrapSmall(score.headline, 39, lines, 2);
		for (int i = 0; i < count; i++) {
			smallText(76 + i * 10, lines[i], ui::FG);
		}
	} else {
		ui::title("what Coral read", ui::CORAL);
		for (int i = 0; i < score.bulletCount; i++) {
			drawBullet(ui::TITLE_H + 3 + i * 14, score.bullets[i]);
		}
	}

	drawCaveat();
}

void draw()
{
	switch (screen) {
		case Screen::Detail:
			drawDetail();
			break;
		case Screen::Score:
			drawScore();
			break;
		default:
			drawList();
			break;
	}
}

// ---------------------------------------------------------------- lifecycle

void enter()
{
	screen = Screen::List;
	page = 0;
	if (!net::online()) {
		listState = agentCount > 0 ? State::Ready : State::Empty;
		return;
	}
	const bool stale = agentCount == 0 || millis() - fetchedAt > STALE_MS;
	if (stale) {
		want(Job::Profiles);
	} else {
		listState = State::Ready;
		view::note("enter opens, r refreshes");
	}
}

void leave()
{
	job = Job::None;
}

void tick()
{
	// Opening the view before the radio has landed is normal, and leaving it
	// showing "needs wifi" until somebody presses a key would be a poor
	// welcome. This is not a poll: it fires once, on the way from Empty.
	if (job == Job::None && listState == State::Empty && net::online()) {
		want(Job::Profiles);
		return;
	}
	if (job == Job::None || !waitingShown) {
		return;
	}
	const Job running = job;
	job = Job::None;
	waitingShown = false;

	if (running == Job::Profiles) {
		fetchProfiles();
		if (listState == State::Ready) {
			view::note("enter opens, r refreshes");
		}
	} else {
		fetchScore();
	}
	view::repaint();
}

// ---------------------------------------------------------------------- keys

bool listKey(const view::Key &k)
{
	if (k.ch == 'r' || k.ch == 'R') {
		if (net::online()) {
			want(Job::Profiles);
		} else {
			view::note("no wifi");
		}
		return true;
	}
	if (listState != State::Ready || agentCount == 0) {
		return false;
	}
	if (k.up) {
		pick = (pick + agentCount - 1) % agentCount;
	} else if (k.down) {
		pick = (pick + 1) % agentCount;
	} else if (k.enter || k.right) {
		screen = Screen::Detail;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool detailKey(const view::Key &k)
{
	if (k.del || k.left) {
		screen = Screen::List;
	} else if (k.up) {
		pick = (pick + agentCount - 1) % agentCount;
	} else if (k.down) {
		pick = (pick + 1) % agentCount;
	} else if (k.enter || k.right) {
		if (!agents[pick].hasToken) {
			view::note("this agent has no token");
			return true;
		}
		screen = Screen::Score;
		page = 0;
		// A score already in hand for this token is worth keeping: the lookup
		// is slow and the answer is good for ten minutes at the source.
		if (scoreState == State::Ready && strcmp(score.address, agents[pick].address) == 0) {
			view::repaint();
			return true;
		}
		want(Job::Scoring);
		return true;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool scoreKey(const view::Key &k)
{
	if (k.del || k.left) {
		screen = Screen::Detail;
	} else if (k.up || k.down) {
		page = page == 0 ? 1 : 0;
	} else if (k.enter && scoreState == State::Failed) {
		want(Job::Scoring);
		return true;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool key(const view::Key &k)
{
	switch (screen) {
		case Screen::Detail:
			return detailKey(k);
		case Screen::Score:
			return scoreKey(k);
		default:
			return listKey(k);
	}
}

const view::View kBankr = {
    .name = "Bankr",
    .source = "BANKR",
    .order = view::ORDER_BANKR,
    .icon = icons::LANDMARK,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kBankr);
