#include <ArduinoJson.h>

#include "../coral.h"
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

constexpr int MAX_AGENTS = 20;
constexpr int LIST_ROWS = 7;  // row 0 says what the ranking is
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

coral::Score score;

// ------------------------------------------------------------------ helpers

void copyField(JsonVariantConst value, char *out, size_t n, const char *fallback = "")
{
	ui::asciify(value.is<const char *>() ? value.as<const char *>() : fallback, out, n);
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
	scoreStatus = coral::fetch(a.chain, a.address, score);
	scoreState = scoreStatus == 200 ? State::Ready : State::Failed;
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
		ui::fit(label, ui::W - 6 - reserved, name, sizeof(name));

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

void drawScore()
{
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
		coral::drawRead(score, a.symbol[0] == ' ' ? a.name : a.symbol);
	} else {
		coral::drawWhy(score);
	}
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
		if (scoreState == State::Ready && score.holds(agents[pick].chain, agents[pick].address)) {
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
