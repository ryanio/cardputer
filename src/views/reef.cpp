#include <ArduinoJson.h>
#include <math.h>

#include "../coral.h"
#include "../motion.h"
#include "../net.h"
#include "../store.h"
#include "../ui.h"
#include "../view.h"

// Guess what Coral thinks of a token before it tells you.
//
// The clues are the market facts the score was computed over, and the score is
// the answer, which is why the API splits them across two endpoints: show one,
// hide the other. Reading holders, liquidity and the buyer to seller split and
// landing near the number is a judgment call. Recognizing a ticker is not,
// which is why the daily round is deliberately anonymous.
//
// Three ways in. The daily round is one token an ET day, the same for
// everybody, and it arrives with its answer already inside it, so the round is
// one fetch and then plays with the radio off. A ticker goes through /resolve,
// because typing 42 hex characters on 56 keys is nobody's idea of a game. A
// random token comes out of the graded corpus. Both of those hide the answer
// behind a live score lookup, which is slow and self rate limiting, so it
// happens once, after the guess.
namespace {

constexpr const char *DAILY_URL = "https://api.0xcoral.com/api/v1/guess/daily";
constexpr const char *RESOLVE_URL = "https://api.0xcoral.com/api/v1/resolve?q=%s";
constexpr const char *TOKEN_URL = "https://api.0xcoral.com/api/v1/tokens/%s/%s";
constexpr const char *INDEX_URL = "https://api.0xcoral.com/api/v1/tokens/index?limit=8";

constexpr const char *DAY_KEY = "reef.day";
constexpr const char *GUESS_KEY = "reef.guess";
constexpr const char *ANSWER_KEY = "reef.answer";
constexpr const char *STREAK_KEY = "reef.streak";

constexpr int MAX_CLUES = 6;
constexpr int TICKER_MAX = 12;
constexpr int GUESS_MAX = 100;
// Inside ten keeps a streak. Wide enough that reading the clues pays, narrow
// enough that a shrug does not.
constexpr int STREAK_BAND = 10;

struct Clue {
	char label[12];
	char value[16];
};

enum class Screen : uint8_t { Home, Ticker, Clues, Reveal };
enum class State : uint8_t { Idle, Waiting, Ready, Failed };
enum class Job : uint8_t { None, Daily, Resolve, Token, Index, Scoring };

Screen screen = Screen::Home;
State state = State::Idle;

// A fetch blocks for as long as the request takes, so it cannot start in the
// pass that asks for it or the waiting screen never reaches the panel. draw()
// marks that it has painted and tick() goes only then.
Job job = Job::None;
bool waitingShown = false;
int status = 0;

int mode = 0;  // the three ways in, in the order they are drawn

// The round in play.
bool daily = false;
char label[24] = {0};
char date[12] = {0};
char chain[16] = {0};
char address[64] = {0};
char symbol[20] = {0};  // learned from the token, and kept back until the reveal
Clue clues[MAX_CLUES];
int clueCount = 0;
coral::Score score;

int guess = -1;
// Whether the number on screen is coming from the wrist. Typing takes it back,
// so the two ways in never fight over the same digit.
bool tilting = false;
int page = 0;  // the reveal: 0 is the number, 1 is what Coral read
char entry[TICKER_MAX + 1] = {0};

// Kept across a reboot, so a round survives the battery going flat.
int streak = 0;
char playedDay[12] = {0};

// ------------------------------------------------------------------ helpers

// A count in the width a clue cell has: 741315 holders is 741K, and 97 is 97.
void count(long v, char *out, size_t n)
{
	if (v < 0) {
		snprintf(out, n, "unknown");
	} else if (v >= 1000000) {
		snprintf(out, n, "%.1fM", (double)v / 1e6);
	} else if (v >= 10000) {
		snprintf(out, n, "%.0fK", (double)v / 1e3);
	} else {
		snprintf(out, n, "%ld", v);
	}
}

void addClue(const char *name, const char *value)
{
	if (clueCount >= MAX_CLUES) {
		return;
	}
	snprintf(clues[clueCount].label, sizeof(clues[0].label), "%s", name);
	snprintf(clues[clueCount].value, sizeof(clues[0].value), "%s", value);
	clueCount++;
}

void addUsd(const char *name, JsonVariantConst value)
{
	char text[16];
	ui::usd(value.isNull() ? 0.0f : value.as<float>(), text, sizeof(text));
	addClue(name, text);
}

void addCount(const char *name, JsonVariantConst value)
{
	char text[16];
	count(value.isNull() ? -1 : value.as<long>(), text, sizeof(text));
	addClue(name, text);
}

// share arrives as 0 to 1 from the daily round and as 0 to 100 from the token
// endpoint, which is the kind of difference that draws a 2400% concentration
// if nobody looks.
void addShare(const char *name, JsonVariantConst value, float scale)
{
	char text[16];
	if (value.isNull()) {
		snprintf(text, sizeof(text), "unknown");
	} else {
		snprintf(text, sizeof(text), "%.0f%%", value.as<float>() * scale);
	}
	addClue(name, text);
}

// Days between two YYYY-MM-DD strings, or -1 if either is not one. Nothing
// here asks the device what day it is: the round says so itself, which keeps
// the streak honest on a unit whose clock never landed.
long daysBetween(const char *from, const char *to)
{
	struct tm a = {};
	struct tm b = {};
	if (strlen(from) < 10 || strlen(to) < 10) {
		return -1;
	}
	if (sscanf(from, "%4d-%2d-%2d", &a.tm_year, &a.tm_mon, &a.tm_mday) != 3 ||
	    sscanf(to, "%4d-%2d-%2d", &b.tm_year, &b.tm_mon, &b.tm_mday) != 3) {
		return -1;
	}
	a.tm_year -= 1900;
	b.tm_year -= 1900;
	a.tm_mon -= 1;
	b.tm_mon -= 1;
	a.tm_hour = b.tm_hour = 12;  // noon, so a daylight saving shift cannot move the day
	const time_t at = mktime(&a);
	const time_t bt = mktime(&b);
	if (at == (time_t)-1 || bt == (time_t)-1) {
		return -1;
	}
	return (long)((bt - at) / 86400);
}

int delta()
{
	return guess < 0 || score.value < 0 ? -1 : abs(score.value - guess);
}

const char *verdictOnGuess()
{
	const int d = delta();
	if (d < 0) {
		return "";
	}
	if (d <= 3) {
		return "sharp";
	}
	if (d <= STREAK_BAND) {
		return "close";
	}
	if (d <= 25) {
		return "in the area";
	}
	return "not this time";
}

uint16_t guessColor()
{
	const int d = delta();
	if (d < 0) {
		return ui::DIM;
	}
	return d <= 3 ? ui::GOOD : (d <= STREAK_BAND ? ui::WARN : ui::DIM);
}

void loadStats()
{
	streak = (int)store::getInt(STREAK_KEY, 0);
	snprintf(playedDay, sizeof(playedDay), "%s", store::getString(DAY_KEY, "").c_str());
}

// Only the daily round carries a streak. A ticker somebody chose is not the
// same token for everybody, so counting it would be counting nothing.
void recordDaily()
{
	if (!daily || date[0] == '\0') {
		return;
	}
	const bool kept = delta() >= 0 && delta() <= STREAK_BAND;
	const long gap = daysBetween(playedDay, date);
	if (!kept) {
		streak = 0;
	} else if (gap == 1) {
		streak++;
	} else {
		streak = 1;  // a first round, or a day was missed
	}

	snprintf(playedDay, sizeof(playedDay), "%s", date);
	store::setString(DAY_KEY, String(date));
	store::setInt(GUESS_KEY, guess);
	store::setInt(ANSWER_KEY, score.value);
	store::setInt(STREAK_KEY, streak);
}

// --------------------------------------------------------------------- fetch

void want(Job next)
{
	job = next;
	waitingShown = false;
	state = State::Waiting;
	view::repaint();
}

void resetRound()
{
	clueCount = 0;
	guess = -1;
	tilting = false;
	page = 0;
	score.value = -1;
	date[0] = '\0';
	symbol[0] = '\0';
}

void readTokenClues(JsonObjectConst token)
{
	clueCount = 0;
	addCount("holders", token["holders"]["count"]);
	addUsd("liquidity", token["market"]["liquidityUsd"]);
	addUsd("mcap", token["market"]["marketCapUsd"]);
	addShare("top 10", token["holders"]["topHoldersExInfraPct"], 1.0f);
	addUsd("24h vol", token["market"]["volume24hUsd"]);
	addShare("24h", token["market"]["priceChange24hPct"], 100.0f);
}

void fetchDaily()
{
	JsonDocument doc;
	const net::Result r = net::getJson(DAILY_URL, doc);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}

	resetRound();
	daily = true;
	snprintf(label, sizeof(label), "today");
	snprintf(date, sizeof(date), "%s", doc["date"] | "");
	snprintf(chain, sizeof(chain), "%s", doc["token"]["chain"] | "");
	snprintf(address, sizeof(address), "%s", doc["token"]["address"] | "");

	JsonObjectConst c = doc["clues"];
	clueCount = 0;
	addCount("holders", c["holderCount"]);
	addUsd("liquidity", c["liquidityUsd"]);
	addUsd("mcap", c["marketCapUsd"]);
	addShare("top 10", c["top10HolderShare"], 100.0f);
	addCount("buyers", c["uniqueBuyers24h"]);
	addCount("sellers", c["uniqueSellers24h"]);

	// The answer rides along with the clues, which is what lets the round be
	// played with the radio off. It is not drawn until a guess is in.
	coral::read(doc["answer"], chain, address, score);

	state = State::Ready;

	// Already played today: show what happened rather than asking again.
	if (strcmp(playedDay, date) == 0) {
		guess = (int)store::getInt(GUESS_KEY, -1);
		screen = guess >= 0 ? Screen::Reveal : Screen::Clues;
	} else {
		screen = Screen::Clues;
	}
}

void fetchToken()
{
	char url[192];
	snprintf(url, sizeof(url), TOKEN_URL, chain, address);

	JsonDocument filter;
	JsonObject market = filter["market"].to<JsonObject>();
	market["liquidityUsd"] = true;
	market["marketCapUsd"] = true;
	market["volume24hUsd"] = true;
	market["priceChange24hPct"] = true;
	JsonObject holders = filter["holders"].to<JsonObject>();
	holders["count"] = true;
	holders["topHoldersExInfraPct"] = true;
	filter["symbol"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}

	readTokenClues(doc.as<JsonObjectConst>());
	// A random round is anonymous the same way the daily one is, so what it
	// was gets held back until the guess is in.
	ui::asciify(doc["symbol"] | "", symbol, sizeof(symbol));
	state = State::Ready;
	screen = Screen::Clues;
}

void fetchResolve()
{
	char url[128];
	char query[TICKER_MAX * 3 + 1];
	// A ticker is letters and digits. Anything else was a typo, not a token.
	size_t at = 0;
	for (const char *p = entry; *p != '\0' && at + 1 < sizeof(query); p++) {
		if (isalnum((unsigned char)*p)) {
			query[at++] = *p;
		}
	}
	query[at] = '\0';
	snprintf(url, sizeof(url), RESOLVE_URL, query);

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}

	JsonObjectConst resolved = doc["resolved"];
	if (resolved.isNull() || !resolved["address"].is<const char *>()) {
		status = net::ERR_PARSE;
		state = State::Failed;
		return;
	}

	resetRound();
	daily = false;
	snprintf(chain, sizeof(chain), "%s", resolved["chain"] | "");
	snprintf(address, sizeof(address), "%s", resolved["address"] | "");
	snprintf(label, sizeof(label), "%s", resolved["symbol"] | query);
	want(Job::Token);
}

void fetchIndex()
{
	JsonDocument doc;
	const net::Result r = net::getJson(INDEX_URL, doc);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}

	JsonArrayConst tokens = doc["tokens"];
	const int total = (int)tokens.size();
	if (total == 0) {
		status = net::ERR_PARSE;
		state = State::Failed;
		return;
	}

	// No seed worth the name on a device that just booted, and none needed:
	// the corpus is small and any entry is as good a round as any other.
	JsonObjectConst pickd = tokens[(int)(millis() % (uint32_t)total)];
	resetRound();
	daily = false;
	snprintf(chain, sizeof(chain), "%s", pickd["chain"] | "");
	snprintf(address, sizeof(address), "%s", pickd["address"] | "");
	snprintf(label, sizeof(label), "a random one");
	want(Job::Token);
}

void fetchScore()
{
	status = coral::fetch(chain, address, score);
	if (status != 200) {
		state = State::Failed;
		return;
	}
	state = State::Ready;
	screen = Screen::Reveal;
}

// ---------------------------------------------------------------------- draw

void drawHome()
{
	char text[64];
	ui::clearBody();
	ui::title("Reef", ui::CORAL);

	if (playedDay[0] != '\0') {
		const int was = (int)store::getInt(ANSWER_KEY, -1);
		const int said = (int)store::getInt(GUESS_KEY, -1);
		if (was >= 0 && said >= 0) {
			snprintf(text, sizeof(text), "last round %s: %d, you said %d", playedDay, was, said);
		} else {
			snprintf(text, sizeof(text), "last round %s", playedDay);
		}
	} else {
		snprintf(text, sizeof(text), "guess the score from the facts");
	}
	ui::small(3, ui::TITLE_H + 3, text, ui::DIM);

	const char *modes[] = {"today's round", "type a ticker", "a random token"};
	for (int i = 0; i < 3; i++) {
		snprintf(text, sizeof(text), "%s%s", i == mode ? "> " : "  ", modes[i]);
		ui::line(i + 1, text, i == mode ? ui::CORAL : ui::FG);
	}

	if (streak > 0) {
		snprintf(text, sizeof(text), "streak %d, inside %d keeps it", streak, STREAK_BAND);
	} else {
		snprintf(text, sizeof(text), "inside %d starts a streak", STREAK_BAND);
	}
	ui::small(3, 105, text, ui::DIM);
}

void drawTicker()
{
	char text[64];
	ui::clearBody();
	ui::title("Type a ticker", ui::CORAL);
	snprintf(text, sizeof(text), "%s_", entry);
	ui::line(1, text);
	ui::small(3, 76, "Coral resolves it to a token, so", ui::DIM);
	ui::small(3, 86, "nobody types a contract address.", ui::DIM);
	ui::small(3, 106, "enter looks it up, del erases", ui::DIM);
}

// The guess, and the dial a wrist turns it on.
//
// Typing 87 on this keyboard is two keys and no thought. Leaning the unit to
// 87 is a judgment held in your hand, which is what the round is asking for,
// so a tilt past ten degrees takes the number over and typing takes it back.
// It is drawn on its own because tilting moves it several times a second, and
// repainting the clues under it every time would flicker the whole screen.
constexpr int DIAL_X = 76;
constexpr int DIAL_W = 150;
constexpr int DIAL_Y = 112;
constexpr int NUMBER_Y = 72;
constexpr float TILT_TAKES_OVER = 10.0f;  // degrees

void drawGuess()
{
	char text[8];
	M5GFX &g = ui::gfx();
	// Two strips, with the hint line left alone between them.
	g.fillRect(DIAL_X, NUMBER_Y - 2, ui::W - DIAL_X, 28, ui::BG);
	g.fillRect(DIAL_X - 4, DIAL_Y - 6, DIAL_W + 8, 13, ui::BG);

	// Nothing stands in for a guess that has not been made. A placeholder here
	// draws as a rule and reads as one.
	if (guess >= 0) {
		snprintf(text, sizeof(text), "%d", guess);
		g.setFont(&fonts::DejaVu24);
		g.setTextColor(tilting ? ui::CORAL : ui::FG, ui::BG);
		g.setTextDatum(textdatum_t::top_left);
		g.drawString(text, DIAL_X, NUMBER_Y);
	}

	if (!motion::available()) {
		return;
	}
	g.drawFastHLine(DIAL_X, DIAL_Y, DIAL_W, ui::RULE);
	for (int at = 0; at <= 100; at += 50) {
		g.drawFastVLine(DIAL_X + at * (DIAL_W - 1) / 100, DIAL_Y - 2, 5, ui::RULE);
	}
	if (guess >= 0) {
		const int x = DIAL_X + guess * (DIAL_W - 1) / 100;
		g.fillRect(x - 1, DIAL_Y - 4, 3, 9, tilting ? ui::CORAL : ui::DIM);
	}
}

void drawClues()
{
	ui::clearBody();
	ui::title(daily ? "Today's round" : label, ui::CORAL);

	// Two columns of three. The row grid is too coarse for six facts and a
	// number big enough to read, so the clues go in small type and the guess
	// gets the space.
	for (int i = 0; i < clueCount; i++) {
		const int column = i / 3;
		const int row = i % 3;
		const int x = 3 + column * 123;
		const int y = ui::TITLE_H + 4 + row * 14;
		ui::small(x, y, clues[i].label, ui::DIM);
		ui::small(x + 58, y, clues[i].value, ui::FG);
	}

	M5GFX &g = ui::gfx();
	g.drawFastHLine(0, 68, ui::W, ui::RULE);

	ui::small(3, 82, "your guess", ui::DIM);
	drawGuess();

	// One line that stays true either way, because the number under it moves
	// with the wrist and the line is not redrawn while it does.
	if (motion::available()) {
		ui::small(3, 98, "tilt or type it, enter reveals", ui::DIM);
	} else {
		ui::small(3, 98, guess >= 0 ? "enter reveals what Coral said" : "type 0 to 100, del erases",
		          ui::DIM);
	}
}

void drawReveal()
{
	char text[64];
	ui::clearBody();

	if (page == 1) {
		coral::drawWhy(score);
		return;
	}

	ui::title("Coral said", ui::CORAL);

	snprintf(text, sizeof(text), "%d", score.value);
	ui::bigNumber(text, coral::verdictColor(score.verdict), score.verdict, ui::TITLE_H + 2);

	snprintf(text, sizeof(text), "you said %d, %d off: %s", guess, delta(), verdictOnGuess());
	ui::small(3, 64, text, guessColor());

	if (daily && streak > 0) {
		snprintf(text, sizeof(text), "%s, streak %d", date, streak);
	} else if (daily) {
		snprintf(text, sizeof(text), "%s, inside %d starts a streak", date, STREAK_BAND);
	} else if (symbol[0] != '\0') {
		snprintf(text, sizeof(text), "it was %s on %s", symbol, chain);
	} else {
		snprintf(text, sizeof(text), "%s on %s", label, chain);
	}
	ui::small(3, 76, text, ui::DIM);
	ui::small(3, 86, "up and down for what Coral read", ui::DIM);

	coral::drawCaveat(score);
}

void draw()
{
	if (state == State::Waiting) {
		const bool slow = job == Job::Scoring;
		ui::message(slow ? "asking Coral" : "reading the round",
		            slow ? "a full lookup takes a few seconds" : "one fetch, then it plays offline",
		            ui::CORAL);
		ui::spinner(ui::W / 2, 96);
		waitingShown = true;
		return;
	}
	if (state == State::Failed) {
		ui::message("Coral had no answer", net::statusText(status), ui::WARN);
		ui::lineAt(105, "del goes back", ui::DIM, textdatum_t::top_center);
		return;
	}

	switch (screen) {
		case Screen::Ticker:
			drawTicker();
			break;
		case Screen::Clues:
			drawClues();
			break;
		case Screen::Reveal:
			drawReveal();
			break;
		default:
			drawHome();
			break;
	}
}

// ---------------------------------------------------------------- lifecycle

void enter()
{
	screen = Screen::Home;
	state = State::Idle;
	mode = 0;
	loadStats();
}

void leave()
{
	job = Job::None;
}

// The wrist, on the one screen that asks for a number. Nothing is submitted by
// leaning, so a unit put down mid round cannot answer for anybody.
void tiltGuess()
{
	if (screen != Screen::Clues || state != State::Ready || !motion::available()) {
		return;
	}
	if (!tilting && fabsf(motion::roll()) < TILT_TAKES_OVER) {
		return;
	}
	tilting = true;
	const int next = constrain((int)lroundf(50.0f + motion::steerX() * 50.0f), 0, GUESS_MAX);
	if (next != guess) {
		guess = next;
		drawGuess();
	}
}

void tick()
{
	tiltGuess();

	if (job == Job::None || !waitingShown) {
		return;
	}
	const Job running = job;
	job = Job::None;
	waitingShown = false;

	switch (running) {
		case Job::Daily:
			fetchDaily();
			break;
		case Job::Resolve:
			fetchResolve();
			break;
		case Job::Token:
			fetchToken();
			break;
		case Job::Index:
			fetchIndex();
			break;
		case Job::Scoring:
			fetchScore();
			break;
		default:
			break;
	}
	view::repaint();
}

// ---------------------------------------------------------------------- keys

bool homeKey(const view::Key &k)
{
	if (k.up) {
		mode = (mode + 2) % 3;
	} else if (k.down) {
		mode = (mode + 1) % 3;
	} else if (k.enter || k.right) {
		if (!net::online()) {
			view::note("needs wifi");
			return true;
		}
		if (mode == 1) {
			entry[0] = '\0';
			screen = Screen::Ticker;
		} else {
			want(mode == 0 ? Job::Daily : Job::Index);
			return true;
		}
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool tickerKey(const view::Key &k)
{
	const size_t length = strlen(entry);
	if (k.enter) {
		if (length > 0) {
			want(Job::Resolve);
		}
		return true;
	}
	if (k.del) {
		if (length == 0) {
			screen = Screen::Home;
		} else {
			entry[length - 1] = '\0';
		}
		view::repaint();
		return true;
	}
	// A ticker is letters and digits, and it reads as upper case everywhere
	// else it appears, so it is typed that way here.
	if (isalnum((unsigned char)k.ch) && length < TICKER_MAX) {
		entry[length] = (char)toupper((unsigned char)k.ch);
		entry[length + 1] = '\0';
		view::repaint();
		return true;
	}
	return false;
}

bool cluesKey(const view::Key &k)
{
	if (k.del) {
		// A typed digit is worth more than a lean, so erasing one puts the
		// number back in the hands of whoever is typing.
		tilting = false;
		if (guess < 0) {
			screen = Screen::Home;
		} else {
			guess = guess < 10 ? -1 : guess / 10;
		}
	} else if (k.ch >= '0' && k.ch <= '9') {
		tilting = false;
		const int digit = k.ch - '0';
		const int next = guess < 0 ? digit : guess * 10 + digit;
		// 100 is a legal answer and 1000 is not, so a third digit only lands
		// when it still leaves a number Coral could have given.
		if (next <= GUESS_MAX) {
			guess = next;
		}
	} else if (k.enter && guess >= 0) {
		page = 0;
		if (daily) {
			// The answer came with the clues, so there is nothing to wait for
			// and nothing to ask anybody. This is the round played offline.
			screen = Screen::Reveal;
			recordDaily();
		} else {
			// A token somebody chose has its answer behind the slow lookup,
			// which happens here, once, after the guess is in.
			want(Job::Scoring);
			return true;
		}
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool revealKey(const view::Key &k)
{
	if (k.del || k.left) {
		screen = Screen::Home;
		loadStats();
	} else if (k.up || k.down) {
		page = page == 0 ? 1 : 0;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool key(const view::Key &k)
{
	if (state == State::Failed && (k.del || k.left)) {
		state = State::Idle;
		screen = Screen::Home;
		view::repaint();
		return true;
	}
	if (state == State::Waiting) {
		return true;  // a fetch is in flight, and there is nothing to press
	}

	switch (screen) {
		case Screen::Ticker:
			return tickerKey(k);
		case Screen::Clues:
			return cluesKey(k);
		case Screen::Reveal:
			return revealKey(k);
		default:
			return homeKey(k);
	}
}

const view::View kReef = {
    .name = "Reef",
    .source = "CORAL",
    .order = view::ORDER_REEF,
    .icon = icons::WAVES,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kReef);
