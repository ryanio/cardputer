#include <new>

#include "../ui.h"
#include "../view.h"

// Breathing, paced by something you watch rather than count. The ring grows on
// the way in, holds, shrinks on the way out, holds, and the colour follows the
// phase so peripheral vision is enough. A marker runs round the outside once
// per phase, so a hold still has something moving in it. Needs no network and
// no account, which is most of why it belongs on a device you keep on the desk.
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
constexpr int RADIUS_MAX = 36;
constexpr int RING_W = 3;

// The marker runs outside the ring at its widest, with room for the ring to
// grow into and none of it landing on the phase line below.
constexpr int ORBIT_R = 41;
constexpr int ORBIT_SLOTS = 72;  // five degrees a step
constexpr int ORBIT_TRAIL = 5;   // the head and four fading behind it
constexpr int TEXT_Y = CENTRE_Y + ORBIT_R + 5;

int pattern = 0;
bool running = false;
Phase phase = IN;
uint32_t phaseStarted = 0;
uint32_t cycles = 0;
int line = 0;
int lastRadius = -1;
uint8_t lastPhase = 255;
int lastRemaining = -1;
long lastCycles = -1;
long orbitAt = -1;

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

// ------------------------------------------------------------------- the gong
//
// A struck gong rather than a beep. The speaker plays samples, so the strike is
// built once on the way in: a fundamental, three partials off the harmonic
// series that leave before it does, and a decay under all of them. Playing the
// same buffer slower is lower and longer, which is what a heavier disc sounds
// like, so the out breath gets that and the in breath gets the small one.
constexpr uint32_t GONG_RATE = 8000;
constexpr size_t GONG_LEN = GONG_RATE;  // a second at the in breath's pitch
constexpr float GONG_HZ = 294.0f;

int8_t *gong = nullptr;

void buildGong()
{
	if (gong != nullptr) {
		return;
	}
	gong = new (std::nothrow) int8_t[GONG_LEN];
	if (gong == nullptr) {
		return;
	}
	for (size_t i = 0; i < GONG_LEN; i++) {
		const float t = (float)i / (float)GONG_RATE;
		const float w = 2.0f * (float)PI * GONG_HZ * t;
		// Inharmonic, which is what stops it sounding like an organ. The high
		// partials are the strike and they are gone within a quarter second.
		float v = sinf(w) * 0.55f + sinf(w * 1.34f) * 0.18f * expf(-t * 4.0f) +
		          sinf(w * 2.76f) * 0.26f * expf(-t * 6.0f) +
		          sinf(w * 5.40f) * 0.12f * expf(-t * 11.0f);
		v *= expf(-t * 3.0f) * (1.0f - expf(-t * 500.0f));
		// Faded to nothing at the end, or the buffer running out is a click.
		if (t > 0.88f) {
			v *= (1.0f - t) / 0.12f;
		}
		gong[i] = (int8_t)constrain((int)(v * 127.0f), -127, 127);
	}
}

void freeGong()
{
	if (gong == nullptr) {
		return;
	}
	// The mixer task reads this buffer while it plays, so it has to be done
	// with it before the memory goes back.
	M5Cardputer.Speaker.stop();
	for (int waited = 0; waited < 50 && M5Cardputer.Speaker.isPlaying(); waited++) {
		delay(1);
	}
	delete[] gong;
	gong = nullptr;
}

void strike(bool in)
{
	if (gong == nullptr) {
		M5Cardputer.Speaker.tone(in ? 392.0f : 262.0f, 120);
		return;
	}
	M5Cardputer.Speaker.playRaw(gong, GONG_LEN, in ? GONG_RATE : GONG_RATE * 3 / 4, false, 1, 0,
	                            true);
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

	// One strike on each turn of the breath and nothing on the holds, which is
	// the whole reason a hold is a hold.
	if (phase == IN || phase == OUT) {
		strike(phase == IN);
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

// --------------------------------------------------------------------- draw

// Font0 in the top right corner, which is the only thing up there that moves.
void drawCount()
{
	M5GFX &g = ui::gfx();
	char text[48];
	g.fillRect(ui::W - 120, 4, 116, 10, ui::BG);
	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_right);
	if (running) {
		snprintf(text, sizeof(text), "%u %s", (unsigned)cycles, cycles == 1 ? "cycle" : "cycles");
	} else {
		snprintf(text, sizeof(text), "%s", current().note);
	}
	g.drawString(text, ui::W - 4, 5);
	lastCycles = (long)cycles;
}

uint16_t fade(uint16_t color, float amount)
{
	const int r = (int)(((color >> 11) & 0x1F) * amount);
	const int g = (int)(((color >> 5) & 0x3F) * amount);
	const int b = (int)((color & 0x1F) * amount);
	return (uint16_t)((r << 11) | (g << 5) | b);
}

void orbitDot(long slot, uint16_t color, int radius)
{
	const long wrapped = ((slot % ORBIT_SLOTS) + ORBIT_SLOTS) % ORBIT_SLOTS;
	const float a =
	    (float)wrapped * (2.0f * (float)PI / ORBIT_SLOTS) - (float)PI / 2.0f;  // 0 is straight up
	ui::gfx().fillCircle(CENTRE_X + (int)lroundf(cosf(a) * ORBIT_R),
	                     CENTRE_Y + (int)lroundf(sinf(a) * ORBIT_R), radius, color);
}

void clearOrbit()
{
	if (orbitAt < 0) {
		return;
	}
	for (long s = orbitAt - ORBIT_TRAIL + 1; s <= orbitAt; s++) {
		orbitDot(s, ui::BG, 2);
	}
	orbitAt = -1;
}

// One lap a phase, so a hold is a marker going round a ring that is not moving
// and you can see how much of it is left without reading the number.
void drawOrbit(uint16_t color)
{
	const long at = (long)(progress() * ORBIT_SLOTS);
	if (at == orbitAt) {
		return;
	}
	long from = orbitAt < 0 ? at : orbitAt;
	if (at - from > ORBIT_SLOTS) {
		from = at - ORBIT_SLOTS;
	}
	// Only the slots the trail has left behind get cleared.
	for (long s = from - ORBIT_TRAIL + 1; s <= at - ORBIT_TRAIL; s++) {
		orbitDot(s, ui::BG, 2);
	}
	for (int i = ORBIT_TRAIL - 1; i >= 0; i--) {
		orbitDot(at - i, i == 0 ? color : fade(color, 0.55f - 0.11f * (float)i), i == 0 ? 2 : 1);
	}
	orbitAt = at;
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
	g.setTextDatum(textdatum_t::top_left);
	// The corner label owns the right edge, so the line has to stop short of it.
	g.drawString(running ? LINES[line] : "arrows pick   enter starts", 4, ui::H - 9);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString("CALM", ui::W - 4, ui::H - 9);

	drawCount();
	lastRadius = -1;
	lastPhase = 255;
	lastRemaining = -1;
	orbitAt = -1;
}

// A solid annulus. Three concentric circles leave holes at the diagonals, which
// on a shape this size reads as a dithered disc rather than a ring.
void band(int inner, int outer, uint16_t color)
{
	if (outer < 1 || outer < inner) {
		return;
	}
	ui::gfx().fillArc(CENTRE_X, CENTRE_Y, max(inner, 1), outer, 0.0f, 360.0f, color);
}

void drawRing()
{
	const int radius = running ? radiusNow() : RADIUS_MIN + 6;
	const uint16_t color = running ? phaseColor(phase) : ui::RULE;

	// The marker restarts at the top of every phase, so the old trail comes off
	// before the new one is anywhere near it.
	if (phase != lastPhase) {
		clearOrbit();
	}

	if (radius != lastRadius) {
		// Only the radii the band has left behind get cleared. Erasing the whole
		// band and drawing it again would flicker, and clearing nothing fills
		// the circle in as it grows.
		const int inner = radius - RING_W + 1;
		if (lastRadius > 0) {
			const int lastInner = lastRadius - RING_W + 1;
			if (radius > lastRadius) {
				band(lastInner, min(lastRadius, inner - 1), ui::BG);
			} else {
				band(max(lastInner, radius + 1), lastRadius, ui::BG);
			}
		}
		band(inner, radius, color);
		lastRadius = radius;
	}

	M5GFX &g = ui::gfx();
	if (running) {
		drawOrbit(color);
	} else {
		clearOrbit();
	}

	if ((long)cycles != lastCycles) {
		drawCount();
	}

	const int remaining =
	    running ? (int)((phaseMs(phase) - (millis() - phaseStarted) + 999) / 1000) : 0;
	if (phase != lastPhase || remaining != lastRemaining) {
		lastPhase = phase;
		lastRemaining = remaining;
		g.fillRect(0, TEXT_Y - 2, ui::W, 19, ui::BG);
		char text[24];
		g.setFont(&fonts::Font2);
		g.setTextDatum(textdatum_t::top_center);
		g.setTextColor(color, ui::BG);
		if (running) {
			snprintf(text, sizeof(text), "%s  %d", phaseName(phase), remaining);
		} else {
			snprintf(text, sizeof(text), "%s", "paused");
		}
		g.drawString(text, CENTRE_X, TEXT_Y);
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
	lastCycles = -1;
	phase = IN;
	M5Cardputer.Speaker.begin();
	M5Cardputer.Speaker.setVolume(110);
	buildGong();
}

void leave()
{
	running = false;
	freeGong();
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
