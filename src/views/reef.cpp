#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

#include "../coral.h"
#include "../motion.h"
#include "../net.h"
#include "../store.h"
#include "../ui.h"
#include "../view.h"

// The reef: what Coral has just graded, who called it, and what the call is
// worth now.
//
// One fetch of tokens/index names the corpus and nothing else, and one fetch
// per token carries everything worth showing: the market, the safety flags,
// Coral's own score, and firstCaller, which is the handle that put the token
// in front of a room first and the market cap it was at when they did. Today's
// cap over that one is the number this view exists for. A call at $129K now
// sitting at $980K is 7.6x, and 7.6x is a thing you can feel without reading
// anything else on the card.
//
// Three tabs, on the tab key.
//
//   Reef     one token a card, arrows cycle, up and down re-rank the whole
//            corpus by whatever you want to see: newest, biggest movers, best
//            calls, biggest.
//   Callers  the handles behind those calls, added up on the device. Coral
//            publishes no leaderboard, so this is one built out of the calls
//            the corpus happens to carry, and it says so.
//   Guess    the game this view started as. One token an ET day, the same one
//            for everybody, clues shown and the answer hidden.
//
// Nothing here polls. The index is fetched once, a token is read once, and the
// corpus fills in behind you while you read a card, a token at a time and only
// once you have stopped pressing keys, because a fetch blocks the loop for as
// long as it takes and a key pressed underneath it is a key nobody sees.
//
// The card deliberately shows no Coral score. A score comes with caveats and
// the Coral name attached, which is a rule and not a preference, and honouring
// it costs a quarter of the panel. So the card is the market and the call, and
// enter opens Coral's read of the token drawn by src/coral.cpp, caveats and
// all, where the number belongs.
namespace {

constexpr const char *DAILY_URL = "https://api.0xcoral.com/api/v1/guess/daily";
constexpr const char *RESOLVE_URL = "https://api.0xcoral.com/api/v1/resolve?q=%s";
constexpr const char *TOKEN_URL = "https://api.0xcoral.com/api/v1/tokens/%s/%s";
constexpr const char *INDEX_URL = "https://api.0xcoral.com/api/v1/tokens/index?limit=%d";

constexpr const char *DAY_KEY = "reef.day";
constexpr const char *GUESS_KEY = "reef.guess";
constexpr const char *ANSWER_KEY = "reef.answer";
constexpr const char *STREAK_KEY = "reef.streak";
constexpr const char *SORT_KEY = "reef.sort";

// Thirty two cards is more than anybody scrolls in a sitting and small enough
// that the whole corpus fits in RAM with room for the TLS session beside it.
constexpr int MAX_TOKENS = 32;
constexpr int MAX_CALLERS = MAX_TOKENS;

constexpr int MAX_CLUES = 6;
constexpr int TICKER_MAX = 12;
constexpr int GUESS_MAX = 100;
// Inside ten keeps a streak. Wide enough that reading the clues pays, narrow
// enough that a shrug does not.
constexpr int STREAK_BAND = 10;

// A card is read for about this long before the next fetch goes out. Long
// enough that holding an arrow down never races a blocking request, short
// enough that a corpus fills itself while somebody looks at the first token.
constexpr uint32_t IDLE_MS = 300;

// Market caps move, and a multiple computed against yesterday's is a lie told
// confidently. A corpus older than this is refetched when the view opens,
// which is the only automatic request this file makes.
constexpr uint32_t STALE_MS = 10 * 60 * 1000;

struct Token {
	char chain[12];
	char address[46];  // a Solana address is 44 characters
	char symbol[18];
	char caller[24];
	char platform[12];
	float mcap;
	float calledMcap;
	float change24h;  // a fraction: 0.038 is up 3.8%
	float liquidity;
	float volume;
	int32_t holders;
	int32_t buys;
	int32_t sells;
	int32_t mentions;
	int32_t rooms;
	time_t calledAt;
	bool read;    // the token endpoint answered
	bool missed;  // it did not, so stop asking
};

struct Caller {
	char handle[24];
	char platform[12];
	int calls;
	int priced;   // calls with both caps, which is what a multiple needs
	float best;   // the one that ran furthest
	float total;  // summed multiples, for an average across their calls
};

struct Clue {
	char label[12];
	char value[16];
};

enum class Tab : uint8_t { Feed, Callers, Guess };
enum class Screen : uint8_t { Feed, Read, Why, Callers, Calls, Home, Ticker, Clues, Reveal };
enum class State : uint8_t { Idle, Waiting, Ready, Failed };
enum class Job : uint8_t { None, Index, Hydrate, Reading, Daily, Resolve, Token, Scoring };

// The order the cards can be ranked in. Every one of them is a number the card
// itself shows, so a ranking never sorts on something invisible.
enum SortId : int { SORT_FRESH, SORT_MOVERS, SORT_CALLS, SORT_BIG, SORTS };
const char *const SORT_NAME[SORTS] = {"newest", "24h movers", "best calls", "biggest"};

Tab tab = Tab::Feed;
Screen screen = Screen::Feed;
State state = State::Idle;

// A fetch blocks for as long as the request takes, so it cannot start in the
// pass that asks for it or the waiting screen never reaches the panel. draw()
// marks that it has painted and tick() goes only then.
Job job = Job::None;
bool waitingShown = false;
int status = 0;
uint32_t lastKey = 0;

Token tokens[MAX_TOKENS];
int tokenCount = 0;
int readCount = 0;
int order[MAX_TOKENS];  // the ranking, as indices into tokens
uint32_t fetchedAt = 0;
int sortBy = SORT_FRESH;
int pick = 0;
int hydrating = -1;  // the token Job::Hydrate is about to ask for

Caller callers[MAX_CALLERS];
int callerCount = 0;
int callerPick = 0;
int callerTop = 0;

// The score of whichever token is open on the read screen. One at a time: the
// bullets are the bulk of a payload and nothing needs thirty two sets of them.
coral::Score score;

// The game. A round in play, its clues, and what was guessed.
bool daily = false;
char label[24] = {0};
char date[12] = {0};
char chain[16] = {0};
char address[64] = {0};
char symbol[20] = {0};  // learned from the token, and kept back until the reveal
Clue clues[MAX_CLUES];
int clueCount = 0;

int guess = -1;
// Whether the number on screen is coming from the wrist. Typing takes it back,
// so the two ways in never fight over the same digit.
bool tilting = false;
int page = 0;  // the reveal: 0 is the number, 1 is what Coral read
char entry[TICKER_MAX + 1] = {0};

// Kept across a reboot, so a round survives the battery going flat.
int streak = 0;
char playedDay[12] = {0};

// The three ways into a round, in the order they are offered. The daily is
// last because it is the one that is over for the day once it is played.
constexpr int MODES = 3;
enum ModeId : int { MODE_TICKER, MODE_RANDOM, MODE_DAILY };
const char *const MODE_NAME[MODES] = {"type a ticker", "a random token", "the token of the day"};
const char *const MODE_NOTE[MODES][2] = {
    {"a symbol like MEME, resolved by Coral", "so nobody types a contract address"},
    {"one out of the corpus Coral has", "already graded, picked at random"},
    {"one token an ET day, the same one for", "everybody, and it plays with wifi off"},
};

int mode = MODE_TICKER;

// ------------------------------------------------------------------ numbers

// A count in the width a cell has: 741315 holders is 741K, and 97 is 97.
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

// A day's move. Past about triple it stops reading as a percentage and starts
// reading as noise, so it turns into a multiple, which is the same number said
// the way somebody would say it out loud.
void move(float fraction, char *out, size_t n)
{
	const float percent = fraction * 100.0f;
	if (percent >= 200.0f) {
		snprintf(out, n, "%.0fx", 1.0f + fraction);
	} else if (fabsf(percent) >= 10.0f) {
		snprintf(out, n, "%+.0f%%", percent);
	} else {
		snprintf(out, n, "%+.1f%%", percent);
	}
}

// Today's market cap over the one the call went out at. Zero when either side
// is missing, which is the honest answer and not a 0.0x.
float multiple(const Token &t)
{
	if (t.caller[0] == '\0' || t.calledMcap <= 0.0f || t.mcap <= 0.0f) {
		return 0.0f;
	}
	return t.mcap / t.calledMcap;
}

// A call, priced. Up reads as a multiple because that is how a call gets told;
// down reads as a percentage, because nobody says a call went 0.3x.
void multipleText(float m, char *out, size_t n)
{
	if (m <= 0.0f) {
		snprintf(out, n, "--");
	} else if (m >= 10.0f) {
		snprintf(out, n, "%.0fx", m);
	} else if (m >= 1.05f) {
		snprintf(out, n, "%.1fx", m);
	} else if (m >= 0.95f) {
		snprintf(out, n, "flat");
	} else {
		snprintf(out, n, "-%.0f%%", (1.0f - m) * 100.0f);
	}
}

uint16_t multipleColor(float m)
{
	if (m <= 0.0f) {
		return ui::DIM;
	}
	if (m >= 1.05f) {
		return ui::GOOD;
	}
	return m >= 0.95f ? ui::DIM : ui::BAD;
}

// Coral stamps its times in UTC and the device keeps its clock in ET, so the
// zone has to come out of the arithmetic rather than out of mktime, which
// would read a UTC stamp as a local one and lose four hours. Days from civil,
// which is the whole of it.
time_t epochOf(const char *iso)
{
	int y = 0;
	int mo = 0;
	int d = 0;
	int h = 0;
	int mi = 0;
	int s = 0;
	if (iso == nullptr || sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6) {
		return 0;
	}
	y -= mo <= 2;
	const long era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (unsigned)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const long days = era * 146097L + (long)doe - 719468L;
	return (time_t)(days * 86400L + h * 3600L + mi * 60L + s);
}

// How long ago, in the width a line can spare. A unit whose clock never landed
// says so rather than counting from 1970.
void ago(time_t when, char *out, size_t n)
{
	const time_t now = time(nullptr);
	if (when <= 0 || !net::clockSet()) {
		snprintf(out, n, "at some point");
		return;
	}
	long seconds = (long)(now - when);
	if (seconds < 0) {
		seconds = 0;
	}
	if (seconds < 3600) {
		snprintf(out, n, "%ldm ago", seconds / 60);
	} else if (seconds < 86400) {
		snprintf(out, n, "%ldh ago", seconds / 3600);
	} else {
		snprintf(out, n, "%ldd ago", seconds / 86400);
	}
}

// ------------------------------------------------------------------ ranking

// What a token is worth under the ranking in force. Anything unread, or
// missing the number being ranked on, comes back false and sinks to the
// bottom in index order rather than sorting as a zero.
bool rankOf(const Token &t, float &out)
{
	if (!t.read) {
		return false;
	}
	switch (sortBy) {
		case SORT_MOVERS:
			out = t.change24h;
			return t.mcap > 0.0f;
		case SORT_CALLS:
			out = multiple(t);
			return out > 0.0f;
		case SORT_BIG:
			out = t.mcap;
			return t.mcap > 0.0f;
		default:
			return false;
	}
}

// Rebuild the ranking, keeping whichever card is on screen under the cursor.
// The corpus fills in behind the reader, so this runs again every time a token
// lands, and a cursor that jumped on every fetch would make the view unusable.
void reorder()
{
	char held[sizeof(tokens[0].address)] = {0};
	if (tokenCount > 0 && pick >= 0 && pick < tokenCount && order[pick] < tokenCount) {
		snprintf(held, sizeof(held), "%s", tokens[order[pick]].address);
	}

	for (int i = 0; i < tokenCount; i++) {
		order[i] = i;
	}

	if (sortBy != SORT_FRESH) {
		// Insertion sort: thirty two entries, and it keeps index order among
		// everything the ranking cannot separate, which is what makes the
		// unread tail stay put instead of shuffling as it fills.
		for (int i = 1; i < tokenCount; i++) {
			const int held_index = order[i];
			float key = 0.0f;
			const bool ranked = rankOf(tokens[held_index], key);
			int at = i;
			while (at > 0) {
				float other = 0.0f;
				const bool otherRanked = rankOf(tokens[order[at - 1]], other);
				if (otherRanked && (!ranked || other >= key)) {
					break;
				}
				if (!otherRanked && !ranked) {
					break;
				}
				order[at] = order[at - 1];
				at--;
			}
			order[at] = held_index;
		}
	}

	if (held[0] == '\0') {
		return;
	}
	for (int i = 0; i < tokenCount; i++) {
		if (strcmp(tokens[order[i]].address, held) == 0) {
			pick = i;
			return;
		}
	}
}

// The callers board, added up from whatever calls the corpus carries. Coral
// publishes no leaderboard, so this is not one: it is the handles behind the
// tokens on this device, which is why every screen that shows it says how many
// tokens it was counted from.
void tallyCallers()
{
	callerCount = 0;
	for (int i = 0; i < tokenCount; i++) {
		const Token &t = tokens[i];
		if (!t.read || t.caller[0] == '\0') {
			continue;
		}
		Caller *found = nullptr;
		for (int c = 0; c < callerCount; c++) {
			if (strcmp(callers[c].handle, t.caller) == 0) {
				found = &callers[c];
				break;
			}
		}
		if (found == nullptr) {
			if (callerCount >= MAX_CALLERS) {
				continue;
			}
			found = &callers[callerCount++];
			snprintf(found->handle, sizeof(found->handle), "%s", t.caller);
			snprintf(found->platform, sizeof(found->platform), "%s", t.platform);
			found->calls = 0;
			found->priced = 0;
			found->best = 0.0f;
			found->total = 0.0f;
		}
		found->calls++;
		const float m = multiple(t);
		if (m > 0.0f) {
			found->priced++;
			found->total += m;
			if (m > found->best) {
				found->best = m;
			}
		}
	}

	// Ranked by how their calls have actually gone, averaged, so four calls
	// that all worked beat one that did. A handle whose calls cannot be
	// priced still gets a row: they called it, that is a fact, and the row
	// says the multiple is unknown rather than pretending it is zero.
	for (int i = 1; i < callerCount; i++) {
		const Caller held = callers[i];
		const float key = held.priced > 0 ? held.total / (float)held.priced : -1.0f;
		int at = i;
		while (at > 0) {
			const Caller &other = callers[at - 1];
			const float otherKey = other.priced > 0 ? other.total / (float)other.priced : -1.0f;
			if (otherKey >= key) {
				break;
			}
			callers[at] = callers[at - 1];
			at--;
		}
		callers[at] = held;
	}

	if (callerPick >= callerCount) {
		callerPick = callerCount > 0 ? callerCount - 1 : 0;
	}
	if (callerPick < callerTop) {
		callerTop = callerPick;
	}
}

float callerAverage(const Caller &c)
{
	return c.priced > 0 ? c.total / (float)c.priced : 0.0f;
}

// --------------------------------------------------------------------- fetch

void want(Job next)
{
	job = next;
	waitingShown = false;
	state = State::Waiting;
	view::repaint();
}

void copyField(JsonVariantConst value, char *out, size_t n, const char *fallback = "")
{
	ui::asciify(value.is<const char *>() ? value.as<const char *>() : fallback, out, n);
}

// Everything the card, the ranking and the callers board read out of one token
// payload.
void tokenFilter(JsonDocument &into)
{
	into["symbol"] = true;
	JsonObject market = into["market"].to<JsonObject>();
	market["marketCapUsd"] = true;
	market["liquidityUsd"] = true;
	market["volume24hUsd"] = true;
	market["priceChange24hPct"] = true;
	JsonObject holders = into["holders"].to<JsonObject>();
	holders["count"] = true;
	holders["topHoldersExInfraPct"] = true;
	JsonObject caller = into["firstCaller"].to<JsonObject>();
	caller["handle"] = true;
	caller["platform"] = true;
	caller["calledAt"] = true;
	caller["calledMcapUsd"] = true;
	JsonObject reach = into["reach"].to<JsonObject>();
	reach["mentions"] = true;
	reach["communities"] = true;
	into["activity"]["h24"]["buys"] = true;
	into["activity"]["h24"]["sells"] = true;
}

// The score rides inside the token payload, which is why nothing in the feed
// has to touch the slow /score route to know what Coral thinks. coral::filter
// only knows how to fill a whole document, not a member of one, so the same
// fields are spelled out here, nested where the token endpoint puts them.
void addScoreFilter(JsonDocument &into)
{
	JsonObject s = into["score"].to<JsonObject>();
	s["score"] = true;
	s["verdict"] = true;
	s["confidence"] = true;
	s["confidenceLabel"] = true;
	JsonObject explanation = s["explanation"].to<JsonObject>();
	explanation["headline"] = true;
	explanation["bullets"] = true;
	explanation["caveats"] = true;
}

void readToken(JsonObjectConst doc, Token &t)
{
	copyField(doc["symbol"], t.symbol, sizeof(t.symbol), "?");
	JsonObjectConst market = doc["market"];
	t.mcap = market["marketCapUsd"] | 0.0f;
	t.liquidity = market["liquidityUsd"] | 0.0f;
	t.volume = market["volume24hUsd"] | 0.0f;
	t.change24h = market["priceChange24hPct"] | 0.0f;
	t.holders = doc["holders"]["count"] | -1;
	t.buys = doc["activity"]["h24"]["buys"] | -1;
	t.sells = doc["activity"]["h24"]["sells"] | -1;

	JsonObjectConst reach = doc["reach"];
	t.mentions = reach.isNull() ? -1 : (reach["mentions"] | -1);
	t.rooms = reach.isNull() ? -1 : (reach["communities"] | -1);

	// firstCaller is present on about a third of the corpus and absent, not
	// null, on the rest. calledMcapUsd goes missing on its own too, which is a
	// call with no entry to price it against rather than a call that never
	// happened, so the handle stays and the multiple does not.
	JsonObjectConst caller = doc["firstCaller"];
	t.caller[0] = '\0';
	t.platform[0] = '\0';
	t.calledMcap = 0.0f;
	t.calledAt = 0;
	if (!caller.isNull()) {
		copyField(caller["handle"], t.caller, sizeof(t.caller));
		copyField(caller["platform"], t.platform, sizeof(t.platform), "somewhere");
		t.calledMcap = caller["calledMcapUsd"] | 0.0f;
		t.calledAt = epochOf(caller["calledAt"] | "");
	}
	t.read = true;
	t.missed = false;
}

void fetchIndex()
{
	char url[96];
	snprintf(url, sizeof(url), INDEX_URL, MAX_TOKENS);

	JsonDocument filter;
	JsonObject wanted = filter["tokens"][0].to<JsonObject>();
	wanted["chain"] = true;
	wanted["address"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}

	tokenCount = 0;
	readCount = 0;
	for (JsonObjectConst entry : doc["tokens"].as<JsonArrayConst>()) {
		if (tokenCount >= MAX_TOKENS) {
			break;
		}
		Token &t = tokens[tokenCount];
		memset(&t, 0, sizeof(t));
		copyField(entry["chain"], t.chain, sizeof(t.chain));
		copyField(entry["address"], t.address, sizeof(t.address));
		if (t.chain[0] == '\0' || t.address[0] == '\0') {
			continue;
		}
		tokenCount++;
	}

	if (tokenCount == 0) {
		status = net::ERR_PARSE;
		state = State::Failed;
		return;
	}
	pick = 0;
	fetchedAt = millis();
	reorder();
	state = State::Ready;
}

// One token, filled in place. This is the only fetch the feed makes after the
// index, and it runs with no waiting screen over it: the card stays up, the
// counter in the corner moves, and the reader never sees a spinner for a
// token they were not asking about.
void fetchHydrate()
{
	if (hydrating < 0 || hydrating >= tokenCount) {
		state = State::Ready;
		return;
	}
	Token &t = tokens[hydrating];
	hydrating = -1;

	char url[192];
	snprintf(url, sizeof(url), TOKEN_URL, t.chain, t.address);

	JsonDocument filter;
	tokenFilter(filter);
	addScoreFilter(filter);

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	state = State::Ready;
	if (!r.ok()) {
		// One that will not answer is one card that says so, not a dead view.
		t.missed = true;
		return;
	}
	readToken(doc.as<JsonObjectConst>(), t);
	readCount++;
	reorder();
	tallyCallers();
}

// The read behind a card. The token endpoint carries the score whole, so this
// is the fast route rather than /score, and it is the one place in the feed
// that puts a spinner up, because it is the one the reader asked for.
void fetchReading()
{
	if (tokenCount == 0) {
		state = State::Failed;
		return;
	}
	const Token &t = tokens[order[pick]];
	char url[192];
	snprintf(url, sizeof(url), TOKEN_URL, t.chain, t.address);

	JsonDocument filter;
	addScoreFilter(filter);

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}
	coral::read(doc["score"], t.chain, t.address, score);
	if (score.value < 0) {
		status = net::ERR_PARSE;
		state = State::Failed;
		return;
	}
	state = State::Ready;
	screen = Screen::Read;
}

// ----------------------------------------------------------------- the game

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

// The random round takes its token out of the corpus the feed already holds,
// so playing one costs nothing extra once the reef has been opened. An empty
// corpus falls back to fetching the index, which is one request either way.
void fetchRandom()
{
	if (tokenCount == 0) {
		want(Job::Index);
		return;
	}
	// No seed worth the name on a device that just booted, and none needed:
	// any entry in the corpus is as good a round as any other.
	const Token &t = tokens[(int)(millis() % (uint32_t)tokenCount)];
	resetRound();
	daily = false;
	snprintf(chain, sizeof(chain), "%s", t.chain);
	snprintf(address, sizeof(address), "%s", t.address);
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

// ui::small only ever draws from a left edge. A column of numbers wants its
// right edge lined up instead, and Font0 is six pixels a character, so the
// arithmetic is exact rather than measured.
void smallRight(int right, int y, const char *text, uint16_t color)
{
	ui::small(right - (int)strlen(text) * 6, y, text, color);
}

// Text placed against a right edge, or against a background that is not the
// page. A selected row on the callers board is a filled band, and every helper
// in ui:: paints its own background black, which punches a hole through it.
void at(int x, int y, const char *text, uint16_t color, const lgfx::IFont *font,
        textdatum_t datum = textdatum_t::top_left, uint16_t background = ui::BG)
{
	M5GFX &g = ui::gfx();
	g.setFont(font);
	g.setTextColor(color, background);
	g.setTextDatum(datum);
	g.drawString(text, x, y);
}

void rightAt(int right, int y, const char *text, uint16_t color, const lgfx::IFont *font)
{
	at(right, y, text, color, font, textdatum_t::top_right);
}

// A ticker in the largest type it fits in. Most are four or five characters
// and deserve the big font; OCEANODRAKE is eleven and does not get it at the
// cost of running under the number beside it.
void drawSymbol(int x, int y, int budget, const char *text, uint16_t color)
{
	M5GFX &g = ui::gfx();
	g.setFont(&fonts::DejaVu24);
	if ((int)g.textWidth(text) > budget) {
		g.setFont(&fonts::Font2);
	}
	g.setTextColor(color, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString(text, x, y);
}

// The strip along the top of the feed and the callers board: which tab is in
// front, and where in it you are. It is the only thing on either screen that
// says the tab key does anything, so it is never dropped.
void drawTabs(const char *right)
{
	const char *const names[3] = {"REEF", "CALLERS", "GUESS"};
	const Tab which[3] = {Tab::Feed, Tab::Callers, Tab::Guess};
	int x = 3;
	for (int i = 0; i < 3; i++) {
		ui::small(x, 1, names[i], tab == which[i] ? ui::CORAL : ui::RULE);
		x += (int)strlen(names[i]) * 6 + 8;
	}
	if (right != nullptr) {
		smallRight(ui::W - 3, 1, right, ui::DIM);
	}
	ui::gfx().drawFastHLine(0, 11, ui::W, ui::RULE);
}

// What the corner says about the corpus: how much of it has been read, and
// whether anything is being read right now.
void corpusNote(char *out, size_t n)
{
	if (readCount >= tokenCount) {
		snprintf(out, n, "%d read", readCount);
	} else {
		snprintf(out, n, "%d of %d read", readCount, tokenCount);
	}
}

void drawCard()
{
	char text[64];
	char other[32];
	const Token &t = tokens[order[pick]];

	ui::clearBody();
	snprintf(text, sizeof(text), "%d/%d %s", pick + 1, tokenCount, SORT_NAME[sortBy]);
	drawTabs(text);

	if (!t.read) {
		// ui::message clears the body under itself, so the strip goes back on
		// top of it: which tab this is stays true even while a card is blank.
		ui::message(t.missed ? "this one would not answer" : "reading it",
		            t.missed ? "arrows move past it" : "one fetch, in a moment",
		            t.missed ? ui::WARN : ui::CORAL);
		snprintf(text, sizeof(text), "%d/%d %s", pick + 1, tokenCount, SORT_NAME[sortBy]);
		drawTabs(text);
		return;
	}

	drawSymbol(3, 14, 150, t.symbol, ui::CORAL);
	move(t.change24h, text, sizeof(text));
	rightAt(ui::W - 3, 15, text, t.change24h >= 0.0f ? ui::GOOD : ui::BAD, &fonts::Font2);
	smallRight(ui::W - 3, 32, "24h", ui::DIM);

	// Two lines of facts, and Font0 gives thirty nine characters to spend on
	// each. Everything here goes missing on some token, and three unknowns in
	// a row read as a broken card rather than a thin one, so a fact that is
	// not there takes no room at all.
	size_t at = 0;
	at = (size_t)snprintf(text, sizeof(text), "%s", t.chain);
	if (t.mcap > 0.0f) {
		ui::usd(t.mcap, other, sizeof(other));
		at += (size_t)snprintf(text + at, sizeof(text) - at, "   %s mcap", other);
	}
	if (t.holders >= 0) {
		count(t.holders, other, sizeof(other));
		snprintf(text + at, sizeof(text) - at, "   %s holders", other);
	}
	ui::small(3, 42, text, ui::DIM);

	text[0] = '\0';
	at = 0;
	if (t.liquidity > 0.0f) {
		ui::usd(t.liquidity, other, sizeof(other));
		at += (size_t)snprintf(text, sizeof(text), "liq %s", other);
	}
	if (t.volume > 0.0f) {
		ui::usd(t.volume, other, sizeof(other));
		snprintf(text + at, sizeof(text) - at, "%svol %s", at > 0 ? "   " : "", other);
	}
	ui::small(3, 53, text, ui::DIM);

	ui::gfx().drawFastHLine(0, 65, ui::W, ui::RULE);

	if (t.caller[0] != '\0') {
		ui::small(3, 70, "first call", ui::DIM);
		ui::small(69, 70, t.caller, ui::FG);

		ago(t.calledAt, other, sizeof(other));
		if (t.calledMcap > 0.0f) {
			char at[16];
			ui::usd(t.calledMcap, at, sizeof(at));
			snprintf(text, sizeof(text), "%s, %s, at %s", t.platform, other, at);
		} else {
			snprintf(text, sizeof(text), "%s, %s, no cap on record", t.platform, other);
		}
		ui::small(3, 81, text, ui::DIM);

		const float m = multiple(t);
		if (m > 0.0f) {
			multipleText(m, text, sizeof(text));
			rightAt(ui::W - 4, 84, text, multipleColor(m), &fonts::DejaVu24);
			ui::small(3, 100, "since the call", ui::DIM);
		} else {
			ui::small(3, 95, "no cap on record to price the call against", ui::DIM);
		}
	} else {
		ui::small(3, 70, "nobody called this one first", ui::DIM);
		if (t.buys >= 0 || t.sells >= 0) {
			snprintf(text, sizeof(text), "24h flow  %ld buys, %ld sells", (long)t.buys,
			         (long)t.sells);
			ui::small(3, 84, text, ui::FG);
		}
		if (t.mentions > 0) {
			snprintf(text, sizeof(text), "chat  %ld mentions in %ld rooms", (long)t.mentions,
			         (long)t.rooms);
			ui::small(3, 95, text, ui::FG);
		}
	}

	ui::small(3, 113, "arrows move and rank, enter reads", ui::DIM);
}

void drawFeed()
{
	if (tokenCount == 0) {
		ui::message("no corpus yet", "enter asks Coral for one", ui::CORAL);
		drawTabs(nullptr);
		return;
	}
	drawCard();
}

constexpr int CALLER_ROWS = 6;
constexpr int CALLER_TOP = 25;
constexpr int CALLER_PITCH = 16;

void drawCallers()
{
	char text[64];
	char note[32];

	ui::clearBody();
	corpusNote(note, sizeof(note));
	drawTabs(note);

	if (callerCount == 0) {
		ui::message(readCount > 0 ? "no calls in what has been read" : "nothing read yet",
		            readCount > 0 ? "the corpus keeps filling" : "the reef tab starts it",
		            ui::CORAL);
		drawTabs(note);
		return;
	}

	ui::small(3, 14, "who called it first", ui::DIM);
	smallRight(ui::W - 3, 14, "since the call", ui::DIM);

	for (int row = 0; row < CALLER_ROWS; row++) {
		const int index = callerTop + row;
		if (index >= callerCount) {
			break;
		}
		const Caller &c = callers[index];
		const int y = CALLER_TOP + row * CALLER_PITCH;
		const bool here = index == callerPick;
		const uint16_t band = here ? ui::PANEL : ui::BG;
		if (here) {
			ui::gfx().fillRect(0, y - 1, ui::W, CALLER_PITCH, ui::PANEL);
		}

		char cut[24];
		ui::fit(c.handle, 118, cut, sizeof(cut));
		at(3, y, cut, here ? ui::CORAL : ui::FG, &fonts::Font2, textdatum_t::top_left, band);

		snprintf(text, sizeof(text), "%d call%s", c.calls, c.calls == 1 ? "" : "s");
		at(126, y + 4, text, ui::DIM, &fonts::Font0, textdatum_t::top_left, band);

		const float avg = callerAverage(c);
		multipleText(avg, text, sizeof(text));
		at(ui::W - 4, y, text, multipleColor(avg), &fonts::Font2, textdatum_t::top_right, band);
	}
}

void drawCalls()
{
	char text[64];
	char other[24];
	const Caller &c = callers[callerPick];

	ui::clearBody();
	ui::title(c.handle, ui::CORAL);

	const float avg = callerAverage(c);
	if (c.priced > 0) {
		multipleText(avg, other, sizeof(other));
		char best[16];
		multipleText(c.best, best, sizeof(best));
		snprintf(text, sizeof(text), "%s, %d call%s here, %s on average, %s at best", c.platform,
		         c.calls, c.calls == 1 ? "" : "s", other, best);
	} else {
		snprintf(text, sizeof(text), "%s, %d call%s here, none priceable", c.platform, c.calls,
		         c.calls == 1 ? "" : "s");
	}
	char lines[2][ui::WRAP_MAX];
	const int wrapped = ui::wrap(text, 39, lines, 2);
	for (int i = 0; i < wrapped; i++) {
		ui::small(3, 22 + i * 10, lines[i], ui::DIM);
	}

	int row = 0;
	for (int i = 0; i < tokenCount && row < 4; i++) {
		const Token &t = tokens[i];
		if (!t.read || strcmp(t.caller, c.handle) != 0) {
			continue;
		}
		const int y = 46 + row * 17;
		char cut[20];
		ui::fit(t.symbol, 84, cut, sizeof(cut));
		at(3, y, cut, ui::FG, &fonts::Font2);

		if (t.calledMcap > 0.0f) {
			char from[16];
			ui::usd(t.calledMcap, from, sizeof(from));
			ui::usd(t.mcap, other, sizeof(other));
			snprintf(text, sizeof(text), "%s to %s", from, other);
		} else {
			snprintf(text, sizeof(text), "no cap on record");
		}
		ui::small(92, y + 4, text, ui::DIM);

		const float m = multiple(t);
		multipleText(m, text, sizeof(text));
		rightAt(ui::W - 4, y, text, multipleColor(m), &fonts::Font2);
		row++;
	}

	ui::small(3, 114, "counted on this device, del goes back", ui::DIM);
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

void drawHome()
{
	char text[64];
	ui::clearBody();
	ui::title("Guess the score", ui::CORAL);

	if (playedDay[0] != '\0') {
		const int was = (int)store::getInt(ANSWER_KEY, -1);
		const int said = (int)store::getInt(GUESS_KEY, -1);
		if (was >= 0 && said >= 0) {
			snprintf(text, sizeof(text), "last round %s: %d, you said %d", playedDay, was, said);
		} else {
			snprintf(text, sizeof(text), "last round %s", playedDay);
		}
	} else {
		snprintf(text, sizeof(text), "the facts are shown, the score is not");
	}
	ui::small(3, ui::TITLE_H + 3, text, ui::DIM);

	for (int i = 0; i < MODES; i++) {
		snprintf(text, sizeof(text), "%s%s", i == mode ? "> " : "  ", MODE_NAME[i]);
		ui::line(i + 1, text, i == mode ? ui::CORAL : ui::FG);
	}

	// A line about whichever one is under the cursor. "The daily" said nothing
	// about what it is or why it is worth coming back to, and neither did the
	// other two.
	ui::small(3, 82, MODE_NOTE[mode][0], ui::DIM);
	ui::small(3, 92, MODE_NOTE[mode][1], ui::DIM);

	if (streak > 0) {
		snprintf(text, sizeof(text), "streak %d, inside %d keeps it, tab leaves", streak,
		         STREAK_BAND);
	} else {
		snprintf(text, sizeof(text), "inside %d starts a streak, tab leaves", STREAK_BAND);
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

// A waiting screen belongs to whatever the reader asked for. Hydration was not
// asked for, so it never draws one: the card underneath stays up and the
// corner counts.
bool waitingIsVisible()
{
	return job != Job::Hydrate;
}

void draw()
{
	if (state == State::Waiting && waitingIsVisible()) {
		const bool slow = job == Job::Scoring;
		ui::message(slow ? "asking Coral for the score" : "asking Coral",
		            slow ? "a full lookup takes a few seconds" : "one fetch, then it reads offline",
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
		case Screen::Read:
			ui::clearBody();
			coral::drawRead(score, tokenCount > 0 ? tokens[order[pick]].symbol : "it");
			break;
		case Screen::Why:
			ui::clearBody();
			coral::drawWhy(score);
			break;
		case Screen::Callers:
			drawCallers();
			break;
		case Screen::Calls:
			drawCalls();
			break;
		case Screen::Home:
			drawHome();
			break;
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
			drawFeed();
			break;
	}
	waitingShown = true;
}

// ---------------------------------------------------------------- lifecycle

void enter()
{
	tab = Tab::Feed;
	screen = Screen::Feed;
	state = State::Idle;
	mode = MODE_TICKER;
	sortBy = (int)store::getInt(SORT_KEY, SORT_FRESH);
	if (sortBy < 0 || sortBy >= SORTS) {
		sortBy = SORT_FRESH;
	}
	lastKey = millis();
	loadStats();
	if (tokenCount > 0) {
		reorder();
		tallyCallers();
	}
	if (net::online() && (tokenCount == 0 || millis() - fetchedAt > STALE_MS)) {
		want(Job::Index);
	}
}

void leave()
{
	job = Job::None;
	hydrating = -1;
}

// Which token to read next. The one on screen first, because that is the card
// somebody is looking at, then the one they are most likely to reach for, then
// whatever is left in index order so the callers board fills itself.
int nextToRead()
{
	if (tokenCount == 0) {
		return -1;
	}
	const int here = order[pick];
	if (!tokens[here].read && !tokens[here].missed) {
		return here;
	}
	for (int step = 1; step <= 2; step++) {
		const int ahead = order[(pick + step) % tokenCount];
		if (!tokens[ahead].read && !tokens[ahead].missed) {
			return ahead;
		}
	}
	for (int i = 0; i < tokenCount; i++) {
		if (!tokens[i].read && !tokens[i].missed) {
			return i;
		}
	}
	return -1;
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

// The corpus fills itself between keypresses. A fetch blocks the loop, and a
// key pressed underneath one is a key nobody sees, so nothing goes out until
// the reader has been still for a moment.
void fillAhead()
{
	if (job != Job::None || state == State::Failed || !net::online()) {
		return;
	}
	if (screen != Screen::Feed && screen != Screen::Callers) {
		return;
	}
	if (millis() - lastKey < IDLE_MS) {
		return;
	}
	if (tokenCount == 0) {
		want(Job::Index);
		return;
	}
	hydrating = nextToRead();
	if (hydrating >= 0) {
		job = Job::Hydrate;
		waitingShown = true;  // there is no waiting screen to wait for
	}
}

void tick()
{
	tiltGuess();
	fillAhead();

	if (job == Job::None || !waitingShown) {
		return;
	}
	const Job running = job;
	job = Job::None;
	waitingShown = false;

	switch (running) {
		case Job::Index:
			fetchIndex();
			break;
		case Job::Hydrate:
			fetchHydrate();
			break;
		case Job::Reading:
			fetchReading();
			break;
		case Job::Daily:
			fetchDaily();
			break;
		case Job::Resolve:
			fetchResolve();
			break;
		case Job::Token:
			fetchToken();
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

void goTab(Tab next)
{
	tab = next;
	state = State::Idle;
	switch (next) {
		case Tab::Callers:
			tallyCallers();
			screen = Screen::Callers;
			break;
		case Tab::Guess:
			screen = Screen::Home;
			loadStats();
			break;
		default:
			screen = Screen::Feed;
			break;
	}
	view::repaint();
}

bool feedKey(const view::Key &k)
{
	if (tokenCount == 0) {
		if (k.enter || k.right) {
			if (!net::online()) {
				view::note("needs wifi");
				return true;
			}
			want(Job::Index);
			return true;
		}
		return false;
	}

	if (k.right) {
		pick = (pick + 1) % tokenCount;
	} else if (k.left) {
		pick = (pick + tokenCount - 1) % tokenCount;
	} else if (k.up || k.down) {
		sortBy = (sortBy + (k.down ? 1 : SORTS - 1)) % SORTS;
		store::setInt(SORT_KEY, sortBy);
		reorder();
		pick = 0;  // every ranking opens at its own top, or it looks like nothing moved
	} else if (k.enter) {
		if (!tokens[order[pick]].read) {
			view::note("not read yet");
			return true;
		}
		if (!net::online()) {
			view::note("needs wifi");
			return true;
		}
		want(Job::Reading);
		return true;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool readKey(const view::Key &k)
{
	if (k.del || k.left) {
		screen = Screen::Feed;
	} else if (k.up || k.down || k.right) {
		screen = screen == Screen::Read ? Screen::Why : Screen::Read;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool callersKey(const view::Key &k)
{
	if (callerCount == 0) {
		return false;
	}
	if (k.down) {
		callerPick = (callerPick + 1) % callerCount;
	} else if (k.up) {
		callerPick = (callerPick + callerCount - 1) % callerCount;
	} else if (k.enter || k.right) {
		screen = Screen::Calls;
		view::repaint();
		return true;
	} else {
		return false;
	}
	if (callerPick < callerTop) {
		callerTop = callerPick;
	} else if (callerPick >= callerTop + CALLER_ROWS) {
		callerTop = callerPick - CALLER_ROWS + 1;
	}
	view::repaint();
	return true;
}

bool homeKey(const view::Key &k)
{
	if (k.up) {
		mode = (mode + MODES - 1) % MODES;
	} else if (k.down) {
		mode = (mode + 1) % MODES;
	} else if (k.enter || k.right) {
		if (!net::online()) {
			view::note("needs wifi");
			return true;
		}
		if (mode == MODE_TICKER) {
			entry[0] = '\0';
			screen = Screen::Ticker;
		} else if (mode == MODE_RANDOM) {
			fetchRandom();
			return true;
		} else {
			want(Job::Daily);
			return true;
		}
	} else if (k.del || k.left) {
		goTab(Tab::Feed);
		return true;
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
	lastKey = millis();

	if (state == State::Failed && (k.del || k.left)) {
		state = State::Idle;
		screen = tab == Tab::Guess ? Screen::Home
		                           : (tab == Tab::Callers ? Screen::Callers : Screen::Feed);
		view::repaint();
		return true;
	}
	if (state == State::Waiting && waitingIsVisible()) {
		return true;  // a fetch is in flight, and there is nothing to press
	}

	// The tab key is the whole navigation, so it works from anywhere a screen
	// belongs to a tab rather than only from the top of one.
	if (k.tab) {
		goTab(tab == Tab::Feed ? Tab::Callers : (tab == Tab::Callers ? Tab::Guess : Tab::Feed));
		return true;
	}

	switch (screen) {
		case Screen::Read:
		case Screen::Why:
			return readKey(k);
		case Screen::Callers:
			return callersKey(k);
		case Screen::Calls:
			if (k.del || k.left) {
				screen = Screen::Callers;
				view::repaint();
				return true;
			}
			return false;
		case Screen::Home:
			return homeKey(k);
		case Screen::Ticker:
			return tickerKey(k);
		case Screen::Clues:
			return cluesKey(k);
		case Screen::Reveal:
			return revealKey(k);
		default:
			return feedKey(k);
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
