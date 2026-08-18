#include <ArduinoJson.h>

#include "../net.h"
#include "../store.h"
#include "../ui.h"
#include "../view.h"

// A GlyphBot, drawn the way it is actually made.
//
// It is not a picture. It is four lines of Unicode and two colors, which is
// already a display format for a 240x135 panel, and the atlas in src/glyphs.h
// carries the 105 characters the collection draws on. There is a rendered PNG
// of every bot, 3000x2250, and the device never fetches one: that is a large
// raster of four lines of text it already has.
//
// Behind the art is a character sheet the API keeps on a separate endpoint,
// which is easy to miss, so it is fetched only when somebody pages into it.
// Every bot has a faction, a role, a mission with an objective and a threat,
// and three abilities that each cost either time or a resource.
namespace {

constexpr const char *BOT_URL = "https://www.glyphbots.com/api/bot/%d";
constexpr const char *STORY_URL = "https://www.glyphbots.com/api/bot/%d/story";

constexpr int MAX_ID = 11111;  // the collection's size
constexpr int MAX_LINES = 4;
constexpr int MAX_ABILITIES = 3;
constexpr size_t LINE_BYTES = 32;  // seven glyphs of UTF-8, and room to spare
constexpr size_t ID_MAX = 5;
constexpr const char *ID_KEY = "bot.id";

struct Ability {
	char name[24];
	char effect[64];
	char cost[20];
};

enum class Screen : uint8_t { Art, Sheet, Mission, Abilities, Goto };
enum class State : uint8_t { Idle, Waiting, Ready, Failed };
enum class Job : uint8_t { None, Bot, Story };

Screen screen = Screen::Art;
State state = State::Idle;
Job job = Job::None;
bool waitingShown = false;
int status = 0;

int id = 0;
int pending = 0;  // the id being fetched, so a failure can say which

char name[40] = {0};
char lines[MAX_LINES][LINE_BYTES] = {{0}};
int lineCount = 0;
uint16_t background = ui::BG;
uint16_t foreground = ui::FG;
int rarity = 0;
long mints = 0;
bool burned = false;

bool haveStory = false;
int storyFor = -1;
char faction[28] = {0};
char role[36] = {0};
char arc[44] = {0};
char objective[110] = {0};
char threat[110] = {0};
Ability abilities[MAX_ABILITIES];
int abilityCount = 0;

char entry[ID_MAX + 1] = {0};

// ------------------------------------------------------------------ helpers

void copyField(JsonVariantConst value, char *out, size_t n, const char *fallback = "")
{
	ui::asciify(value.is<const char *>() ? value.as<const char *>() : fallback, out, n);
}

int glyphsIn(const char *text)
{
	int count = 0;
	while (ui::nextCodepoint(text) != 0) {
		count++;
	}
	return count;
}

// One cell of bot art. The atlas carries the collection's 105 non ASCII
// glyphs, and the art also uses a plain caret and a lower case o, which the
// panel's own font already has: drawing those from the atlas would mean
// putting the Latin alphabet in it to catch two characters.
void drawCell(uint32_t point, int x, int y, uint16_t color, uint16_t behind)
{
	if (point == ' ' || point == 0) {
		return;
	}
	if (point < 128) {
		M5GFX &g = ui::gfx();
		g.setFont(&fonts::DejaVu24);
		g.setTextColor(color, behind);
		g.setTextDatum(textdatum_t::middle_center);
		const char text[2] = {(char)point, '\0'};
		g.drawString(text, x + ui::GLYPH_CELL / 2, y + ui::GLYPH_CELL / 2);
		return;
	}
	ui::glyph(point, x, y, color);
}

// A text page on a bot's own background, with its name along the top. The art
// page is the only one that gets the whole panel to itself.
void page(const char *heading)
{
	ui::clearAll(ui::BG);
	ui::title(heading, ui::CORAL);
	char text[48];
	snprintf(text, sizeof(text), "GLYPHBOTS %d", id);
	ui::small(3, 124, text, ui::RULE);
}

// --------------------------------------------------------------------- fetch

void want(Job next)
{
	job = next;
	waitingShown = false;
	state = State::Waiting;
	view::repaint();
}

void fetchBot()
{
	char url[80];
	snprintf(url, sizeof(url), BOT_URL, pending);

	JsonDocument filter;
	JsonObject bot = filter["bot"].to<JsonObject>();
	bot["tokenId"] = true;
	bot["name"] = true;
	bot["rarityRank"] = true;
	bot["burnedAt"] = true;
	JsonObject unicode = bot["unicode"].to<JsonObject>();
	unicode["textContent"] = true;
	unicode["colors"] = true;
	bot["royalties"]["mintCount"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}

	JsonObjectConst b = doc["bot"];
	id = b["tokenId"] | pending;
	store::setInt(ID_KEY, id);
	copyField(b["name"], name, sizeof(name), "unnamed");
	rarity = b["rarityRank"] | 0;
	mints = b["royalties"]["mintCount"] | 0L;
	burned = !b["burnedAt"].isNull();

	// The art is UTF-8 and is the one string on this device that is not folded
	// down to ASCII: every character in it is a cell in the atlas.
	lineCount = 0;
	for (JsonVariantConst line : b["unicode"]["textContent"].as<JsonArrayConst>()) {
		if (lineCount >= MAX_LINES) {
			break;
		}
		snprintf(lines[lineCount], LINE_BYTES, "%s", line.as<const char *>());
		lineCount++;
	}

	// Two of every three bots sampled state their colors as hex rather than
	// hsl, so this takes either. A color that will not parse falls back rather
	// than drawing a bot nobody can see.
	if (!ui::parseColor(b["unicode"]["colors"]["background"] | "", background)) {
		background = ui::BG;
	}
	if (!ui::parseColor(b["unicode"]["colors"]["text"] | "", foreground)) {
		foreground = ui::FG;
	}

	haveStory = false;
	state = State::Ready;
}

void fetchStory()
{
	char url[80];
	snprintf(url, sizeof(url), STORY_URL, id);

	JsonDocument filter;
	JsonObject a = filter["story"]["arc"].to<JsonObject>();
	a["title"] = true;
	a["role"] = true;
	a["faction"] = true;
	JsonObject mission = a["mission"].to<JsonObject>();
	mission["objective"] = true;
	mission["threat"] = true;
	JsonObject ability = a["abilities"][0].to<JsonObject>();
	ability["name"] = true;
	ability["effect"] = true;
	ability["cooldown"] = true;
	ability["resource"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	status = r.status;
	if (!r.ok()) {
		state = State::Failed;
		return;
	}

	JsonObjectConst a2 = doc["story"]["arc"];
	copyField(a2["title"], arc, sizeof(arc));
	copyField(a2["role"], role, sizeof(role));
	copyField(a2["faction"], faction, sizeof(faction));
	copyField(a2["mission"]["objective"], objective, sizeof(objective));
	copyField(a2["mission"]["threat"], threat, sizeof(threat));

	abilityCount = 0;
	for (JsonObjectConst item : a2["abilities"].as<JsonArrayConst>()) {
		if (abilityCount >= MAX_ABILITIES) {
			break;
		}
		Ability &out = abilities[abilityCount];
		copyField(item["name"], out.name, sizeof(out.name));
		copyField(item["effect"], out.effect, sizeof(out.effect));
		// An ability costs time or a resource, never both, and the third one
		// always costs the resource.
		if (item["cooldown"].is<const char *>()) {
			copyField(item["cooldown"], out.cost, sizeof(out.cost));
		} else {
			copyField(item["resource"], out.cost, sizeof(out.cost));
		}
		abilityCount++;
	}

	haveStory = true;
	storyFor = id;
	state = State::Ready;
}

void openBot(int next)
{
	pending = next < 1 ? MAX_ID : (next > MAX_ID ? 1 : next);
	want(Job::Bot);
}

// ---------------------------------------------------------------------- draw

void drawArt()
{
	ui::clearAll(background);

	// Every line is centered on a monospace grid, which is what the rendered
	// reference does. Four lines of 32 fill 128 of the 135 rows, and the
	// bottom line of a bot is never more than three glyphs wide, so the corner
	// the name sits in stays empty.
	const int top = (ui::H - MAX_LINES * ui::GLYPH_CELL) / 2;
	for (int row = 0; row < lineCount; row++) {
		const char *text = lines[row];
		const int wide = glyphsIn(text);
		int x = (ui::W - wide * ui::GLYPH_CELL) / 2;
		const int y = top + row * ui::GLYPH_CELL;
		uint32_t point = 0;
		while ((point = ui::nextCodepoint(text)) != 0) {
			drawCell(point, x, y, foreground, background);
			x += ui::GLYPH_CELL;
		}
	}

	M5GFX &g = ui::gfx();
	char text[40];
	snprintf(text, sizeof(text), "GLYPHBOTS %d", id);
	g.setFont(&fonts::Font0);
	g.setTextColor(foreground, background);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString(text, 3, 124);

	if (burned) {
		g.setTextDatum(textdatum_t::top_right);
		g.drawString("burned", ui::W - 3, 124);
	}
}

void drawSheet()
{
	char text[64];
	page(name);

	snprintf(text, sizeof(text), "#%d of %d", id, MAX_ID);
	ui::small(3, 24, text, ui::DIM);
	if (rarity > 0) {
		snprintf(text, sizeof(text), "rarity rank %d", rarity);
		ui::small(3, 34, text, ui::DIM);
	}

	if (!haveStory) {
		ui::small(3, 56, "left and right page the sheet", ui::DIM);
		return;
	}

	ui::small(3, 52, faction, ui::CORAL);
	ui::small(3, 64, role, ui::FG);

	char wrapped[2][ui::WRAP_MAX];
	const int count = ui::wrap(arc, 39, wrapped, 2);
	for (int i = 0; i < count; i++) {
		ui::small(3, 80 + i * 10, wrapped[i], ui::DIM);
	}

	snprintf(text, sizeof(text), "%ld mint%s", mints, mints == 1 ? "" : "s");
	ui::small(3, 106, text, ui::DIM);
}

void drawMission()
{
	page("Mission");
	char wrapped[3][ui::WRAP_MAX];

	ui::small(3, 24, "objective", ui::CORAL);
	int count = ui::wrap(objective, 39, wrapped, 3);
	for (int i = 0; i < count; i++) {
		ui::small(3, 36 + i * 10, wrapped[i], ui::FG);
	}

	ui::small(3, 74, "threat", ui::CORAL);
	count = ui::wrap(threat, 39, wrapped, 3);
	for (int i = 0; i < count; i++) {
		ui::small(3, 86 + i * 10, wrapped[i], ui::DIM);
	}
}

void drawAbilities()
{
	page("Abilities");
	for (int i = 0; i < abilityCount; i++) {
		const int y = 22 + i * 33;
		ui::small(3, y, abilities[i].name, ui::FG);
		ui::small(3 + (int)strlen(abilities[i].name) * 6 + 8, y, abilities[i].cost, ui::CORAL);
		char wrapped[2][ui::WRAP_MAX];
		const int count = ui::wrap(abilities[i].effect, 39, wrapped, 2);
		for (int line = 0; line < count; line++) {
			ui::small(3, y + 11 + line * 10, wrapped[line], ui::DIM);
		}
	}
}

void drawGoto()
{
	char text[48];
	page("Which bot");
	snprintf(text, sizeof(text), "%s_", entry);
	ui::line(1, text);
	snprintf(text, sizeof(text), "1 to %d, enter opens it", MAX_ID);
	ui::small(3, 76, text, ui::DIM);
	ui::small(3, 88, "del erases, r is a random one", ui::DIM);
}

void draw()
{
	if (state == State::Waiting) {
		ui::clearAll(ui::BG);
		char text[40];
		snprintf(text, sizeof(text), "bot %d", pending);
		ui::message("reading", text);
		ui::spinner(ui::W / 2, 100);
		waitingShown = true;
		return;
	}
	if (state == State::Failed) {
		ui::clearAll(ui::BG);
		char text[40];
		snprintf(text, sizeof(text), "bot %d", pending);
		ui::message(text, net::statusText(status), ui::WARN);
		ui::lineAt(108, "r is a random one", ui::DIM, textdatum_t::top_center);
		return;
	}
	if (state == State::Idle) {
		ui::clearAll(ui::BG);
		ui::message("glyphbots", net::online() ? "r opens one" : "needs wifi");
		return;
	}

	switch (screen) {
		case Screen::Sheet:
			drawSheet();
			break;
		case Screen::Mission:
			drawMission();
			break;
		case Screen::Abilities:
			drawAbilities();
			break;
		case Screen::Goto:
			drawGoto();
			break;
		default:
			drawArt();
			break;
	}
}

// ---------------------------------------------------------------- lifecycle

void enter()
{
	screen = Screen::Art;
	entry[0] = '\0';
	if (net::online() && state != State::Ready) {
		// The one you were last looking at, because coming back to a different
		// bot every time makes the collection feel like a slot machine. r is
		// how you ask for one nobody chose.
		openBot((int)store::getInt(ID_KEY, 1));
	}
}

void leave()
{
	job = Job::None;
}

void tick()
{
	if (job == Job::None) {
		if (state == State::Idle && net::online()) {
			openBot((int)store::getInt(ID_KEY, 1));
		}
		return;
	}
	if (!waitingShown) {
		return;
	}
	const Job running = job;
	job = Job::None;
	waitingShown = false;
	if (running == Job::Bot) {
		fetchBot();
	} else {
		fetchStory();
	}
	view::repaint();
}

// ---------------------------------------------------------------------- keys

// The three text pages all need the story, so paging into one fetches it.
void goTo(Screen next)
{
	screen = next;
	if (next != Screen::Art && next != Screen::Goto && (!haveStory || storyFor != id)) {
		want(Job::Story);
	} else {
		view::repaint();
	}
}

bool gotoKey(const view::Key &k)
{
	const size_t length = strlen(entry);
	if (k.enter) {
		if (length > 0) {
			const int wanted = atoi(entry);
			entry[0] = '\0';
			screen = Screen::Art;
			openBot(wanted);
		} else {
			screen = Screen::Art;
			view::repaint();
		}
		return true;
	}
	if (k.del) {
		if (length == 0) {
			screen = Screen::Art;
		} else {
			entry[length - 1] = '\0';
		}
		view::repaint();
		return true;
	}
	if (isdigit((unsigned char)k.ch) && length < ID_MAX) {
		entry[length] = k.ch;
		entry[length + 1] = '\0';
		view::repaint();
		return true;
	}
	return false;
}

bool key(const view::Key &k)
{
	if (state == State::Waiting) {
		return true;
	}
	if (screen == Screen::Goto) {
		return gotoKey(k);
	}

	if (k.ch == 'r' || k.ch == 'R') {
		openBot((int)(millis() % MAX_ID) + 1);
		return true;
	}
	if (isdigit((unsigned char)k.ch)) {
		// Typing a digit anywhere is how you go to a bot by number.
		entry[0] = k.ch;
		entry[1] = '\0';
		screen = Screen::Goto;
		view::repaint();
		return true;
	}
	if (state != State::Ready) {
		return false;
	}

	if (k.right) {
		goTo(screen == Screen::Art       ? Screen::Sheet
		     : screen == Screen::Sheet   ? Screen::Mission
		     : screen == Screen::Mission ? Screen::Abilities
		                                 : Screen::Art);
	} else if (k.left) {
		goTo(screen == Screen::Art         ? Screen::Abilities
		     : screen == Screen::Abilities ? Screen::Mission
		     : screen == Screen::Mission   ? Screen::Sheet
		                                   : Screen::Art);
	} else if (k.up) {
		openBot(id - 1);
	} else if (k.down) {
		openBot(id + 1);
	} else if (k.del) {
		screen = Screen::Art;
		view::repaint();
	} else {
		return false;
	}
	return true;
}

const view::View kBot = {
    .name = "Bot",
    .source = "GLYPHBOTS",
    .order = view::ORDER_BOT,
    .icon = icons::BOT,
    // The art is the whole point, so it takes the panel and this file paints
    // the source name into a corner the bots leave empty.
    .fullScreen = true,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kBot);
