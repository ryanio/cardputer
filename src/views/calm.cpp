#include "../ui.h"
#include "../view.h"

// Breathing, paced by something you watch rather than count. The ring grows on
// the way in, holds, shrinks on the way out, holds, and the colour follows the
// phase so peripheral vision is enough. Needs no network and no account, which
// is most of why it belongs on a device you keep on the desk.
namespace {

enum Phase : uint8_t { IN, HOLD_IN, OUT, HOLD_OUT, PHASES };

struct Pattern {
	const char *name;
	const char *note;
	uint16_t seconds[PHASES];  // in, hold, out, hold
};

// Four that people actually use. Box is the one everyone has heard of, 4-7-8 is
// the one that makes you yawn, coherent is the one that is easiest to keep up,
// and belly is the slow one.
const Pattern PATTERNS[] = {
    {"Box", "even, steady, four counts each", {4, 4, 4, 4}},
    {"4 7 8", "for winding down", {4, 7, 8, 0}},
    {"Coherent", "six breaths a minute", {5, 0, 5, 0}},
    {"Belly", "slow, low, and long out", {4, 2, 8, 0}},
};
constexpr int PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

// One line at a time between cycles. Written for this, not quoted.
const char *LINES[] = {
    "nothing here needs you yet", "the out breath is the long one", "shoulders down, jaw loose",
    "you are allowed to be slow", "let the next one arrive",        "this is the whole task",
};
constexpr int LINE_COUNT = sizeof(LINES) / sizeof(LINES[0]);

constexpr int CENTRE_X = ui::W / 2;
constexpr int CENTRE_Y = 62;
constexpr int RADIUS_MIN = 8;
constexpr int RADIUS_MAX = 40;

int pattern = 0;
bool running = false;
Phase phase = IN;
uint32_t phaseStarted = 0;
uint32_t cycles = 0;
int line = 0;
int lastRadius = -1;
uint8_t lastPhase = 255;
int lastRemaining = -1;

const Pattern &current()
{
	return PATTERNS[pattern];
}

uint16_t phaseMs(Phase p)
{
	return (uint16_t)(current().seconds[p] * 1000);
}

const char *phaseName(Phase p)
{
	switch (p) {
		case IN:
			return "breathe in";
		case HOLD_IN:
			return "hold";
		case OUT:
			return "breathe out";
		default:
			return "rest";
	}
}

uint16_t phaseColor(Phase p)
{
	switch (p) {
		case IN:
			return ui::hsl(180.0f, 0.55f, 0.62f);  // cool, filling
		case HOLD_IN:
			return ui::hsl(210.0f, 0.35f, 0.55f);
		case OUT:
			return ui::CORAL;  // warm, emptying
		default:
			return ui::hsl(255.0f, 0.25f, 0.45f);
	}
}

// Where in the phase we are, 0 to 1.
float progress()
{
	const uint16_t span = phaseMs(phase);
	if (span == 0) {
		return 1.0f;
	}
	const float done = (float)(millis() - phaseStarted) / (float)span;
	return done > 1.0f ? 1.0f : done;
}

int radiusNow()
{
	const float t = progress();
	float fill = 0.0f;
	switch (phase) {
		case IN:
			fill = t;
			break;
		case HOLD_IN:
			fill = 1.0f;
			break;
		case OUT:
			fill = 1.0f - t;
			break;
		default:
			fill = 0.0f;
			break;
	}
	// Ease so the turns feel like breath rather than a metronome.
	const float eased = fill * fill * (3.0f - 2.0f * fill);
	return RADIUS_MIN + (int)((RADIUS_MAX - RADIUS_MIN) * eased);
}

void advance()
{
	int next = (phase + 1) % PHASES;
	// Patterns with a zero length hold skip it rather than flicker.
	while (current().seconds[next] == 0 && next != IN) {
		next = (next + 1) % PHASES;
	}
	if (next == IN) {
		cycles++;
		line = (line + 1) % LINE_COUNT;
	}
	phase = (Phase)next;
	phaseStarted = millis();

	// A short tone on the turn, low going out, higher coming in. The speaker is
	// a real one, so this is a nudge rather than an alarm.
	if (phase == IN) {
		M5Cardputer.Speaker.tone(392.0f, 90);
	} else if (phase == OUT) {
		M5Cardputer.Speaker.tone(262.0f, 120);
	}
}

void start()
{
	running = true;
	phase = IN;
	phaseStarted = millis();
	lastRadius = -1;
	lastPhase = 255;
}

void drawFrame()
{
	char text[48];
	M5GFX &g = ui::gfx();

	ui::clearAll();
	snprintf(text, sizeof(text), "%s", current().name);
	g.setFont(&fonts::Font2);
	g.setTextColor(ui::FG, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString(text, 4, 2);

	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_right);
	if (running) {
		snprintf(text, sizeof(text), "%u cycles", (unsigned)cycles);
	} else {
		snprintf(text, sizeof(text), "%s", current().note);
	}
	g.drawString(text, ui::W - 4, 5);

	g.setTextDatum(textdatum_t::top_left);
	// The corner label owns the right edge, so the line has to stop short of it.
	g.drawString(running ? LINES[line] : "arrows pick   enter starts", 4, ui::H - 9);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString("CALM", ui::W - 4, ui::H - 9);
	lastRadius = -1;
	lastPhase = 255;
	lastRemaining = -1;
}

void drawRing()
{
	M5GFX &g = ui::gfx();
	const int radius = running ? radiusNow() : RADIUS_MIN + 6;
	const uint16_t color = running ? phaseColor(phase) : ui::RULE;

	if (radius != lastRadius) {
		// Only the band that changed gets cleared, so the ring can move every
		// frame without the whole screen flickering.
		if (lastRadius > radius) {
			for (int r = radius + 1; r <= lastRadius; r++) {
				g.drawCircle(CENTRE_X, CENTRE_Y, r, ui::BG);
			}
		}
		for (int r = radius - 2; r <= radius; r++) {
			if (r > 0) {
				g.drawCircle(CENTRE_X, CENTRE_Y, r, color);
			}
		}
		lastRadius = radius;
	}

	const int remaining =
	    running ? (int)((phaseMs(phase) - (millis() - phaseStarted) + 999) / 1000) : 0;
	if (phase != lastPhase || remaining != lastRemaining) {
		lastPhase = phase;
		lastRemaining = remaining;
		g.fillRect(0, CENTRE_Y + RADIUS_MAX + 4, ui::W, 18, ui::BG);
		char text[24];
		g.setFont(&fonts::Font2);
		g.setTextDatum(textdatum_t::top_center);
		g.setTextColor(color, ui::BG);
		if (running) {
			snprintf(text, sizeof(text), "%s  %d", phaseName(phase), remaining);
		} else {
			snprintf(text, sizeof(text), "%s", "paused");
		}
		g.drawString(text, CENTRE_X, CENTRE_Y + RADIUS_MAX + 6);
	}
}

void draw()
{
	drawFrame();
	drawRing();
}

void enter()
{
	running = false;
	cycles = 0;
	phase = IN;
	M5Cardputer.Speaker.begin();
	M5Cardputer.Speaker.setVolume(90);
}

void leave()
{
	running = false;
	M5Cardputer.Speaker.stop();
}

void tick()
{
	if (running && millis() - phaseStarted >= phaseMs(phase)) {
		advance();
	}
	// The ring is redrawn directly rather than through a full repaint: at this
	// size a whole frame would flicker, and the whole point is that it is calm.
	drawRing();
}

bool key(const view::Key &k)
{
	if (k.enter || k.space) {
		if (running) {
			running = false;
			M5Cardputer.Speaker.stop();
		} else {
			start();
		}
		view::repaint();
		return true;
	}
	if (k.up || k.down) {
		pattern =
		    k.up ? (pattern + PATTERN_COUNT - 1) % PATTERN_COUNT : (pattern + 1) % PATTERN_COUNT;
		if (running) {
			start();
		}
		view::repaint();
		return true;
	}
	return false;
}

const view::View kCalm = {
    .name = "Calm",
    .source = "BREATHE",
    .order = view::ORDER_CALM,
    .icon = icons::WIND,
    .fullScreen = true,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kCalm);
