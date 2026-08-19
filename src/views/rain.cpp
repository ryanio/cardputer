#include "../glyphs.h"
#include "../motion.h"
#include "../ui.h"
#include "../view.h"

// The alphabet a GlyphBot is made of, poured out of the box.
//
// The atlas is already on the device for the Bot view: 105 characters the
// collection draws on, rendered once into 32x32 cells. Here they fall. Gravity
// is whichever way the unit is tipped, so pouring them into a corner is a
// wrist, not a keypress, and a shake throws the lot back up and deals a new
// hand of characters.
//
// It is a toy, and it is also the cheapest honest test of the IMU: if the
// axis constants in motion.cpp are wrong, this is the screen where anybody can
// see it in a second.
namespace {

constexpr int CELL = glyphs::CELL;
constexpr int MAX_DROPS = 18;
constexpr int START_DROPS = 11;
constexpr uint32_t STEP_MS = 33;  // 30 a second, which is all a bounce needs

// A step is a step, not a second, so the numbers are small. Gravity is in
// screen pixels per step squared at one g.
constexpr float GRAVITY = 0.85f;
constexpr float BOUNCE = 0.45f;
constexpr float DRAG = 0.995f;
// Under this at an edge it has landed. Without it a pile hums against the
// floor forever, one pixel at a time.
constexpr float REST = 0.35f;

struct Drop {
	float x;
	float y;
	float vx;
	float vy;
	int index;  // into the atlas
	uint16_t color;
	int drawnX;
	int drawnY;
};

Drop drops[MAX_DROPS];
int count = START_DROPS;
uint32_t stepped = 0;

// Small and local, because the point is a scatter nobody can predict, not a
// sequence anybody has to reproduce.
uint32_t seed = 1;

uint32_t nextRandom()
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

int pick(int n)
{
	return (int)(nextRandom() % (uint32_t)n);
}

float unit()
{
	return (float)(nextRandom() % 1000u) / 1000.0f;
}

// Two thirds of the collection's own colors are a hue away from each other
// rather than a palette, so the drops take a hue each and keep the light and
// saturation steady. Coral gets a look in because this is still flint.
uint16_t colorFor(int i)
{
	if (i % 5 == 0) {
		return ui::CORAL;
	}
	return ui::hsl(unit() * 360.0f, 0.55f, 0.62f);
}

void deal(Drop &d, bool placeToo)
{
	d.index = pick(glyphs::COUNT);
	d.color = colorFor(pick(5));
	if (placeToo) {
		d.x = unit() * (float)(ui::W - CELL);
		d.y = unit() * (float)(ui::H - CELL);
		d.drawnX = -CELL;
		d.drawnY = -CELL;
	}
	d.vx = (unit() - 0.5f) * 6.0f;
	d.vy = (unit() - 0.5f) * 6.0f;
}

void fill()
{
	for (int i = 0; i < MAX_DROPS; i++) {
		deal(drops[i], true);
	}
}

// Which way is down, in screen pixels. Flat on a desk, or on a unit with no
// IMU, it is down the panel, so the view still works and still reads as rain.
void gravity(float &ax, float &ay)
{
	if (!motion::available()) {
		ax = 0.0f;
		ay = GRAVITY;
		return;
	}
	ax = motion::gravityX() * GRAVITY;
	ay = motion::gravityY() * GRAVITY;
}

void step()
{
	float ax = 0.0f;
	float ay = 0.0f;
	gravity(ax, ay);

	const float right = (float)(ui::W - CELL);
	const float bottom = (float)(ui::H - CELL);

	for (int i = 0; i < count; i++) {
		Drop &d = drops[i];
		d.vx = (d.vx + ax) * DRAG;
		d.vy = (d.vy + ay) * DRAG;
		d.x += d.vx;
		d.y += d.vy;

		if (d.x < 0.0f) {
			d.x = 0.0f;
			d.vx = fabsf(d.vx) * BOUNCE;
			if (fabsf(d.vx) < REST) {
				d.vx = 0.0f;
			}
		} else if (d.x > right) {
			d.x = right;
			d.vx = -fabsf(d.vx) * BOUNCE;
			if (fabsf(d.vx) < REST) {
				d.vx = 0.0f;
			}
		}
		if (d.y < 0.0f) {
			d.y = 0.0f;
			d.vy = fabsf(d.vy) * BOUNCE;
			if (fabsf(d.vy) < REST) {
				d.vy = 0.0f;
			}
		} else if (d.y > bottom) {
			d.y = bottom;
			d.vy = -fabsf(d.vy) * BOUNCE;
			if (fabsf(d.vy) < REST) {
				d.vy = 0.0f;
			}
		}
	}
}

void drawLabels()
{
	M5GFX &g = ui::gfx();
	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	// The corner label owns the right edge, so this has to stop short of it.
	g.drawString(motion::available() ? "tilt pours, shake scatters" : "no imu: it falls straight",
	             4, ui::H - 9);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString("GLYPHBOTS", ui::W - 4, ui::H - 9);
}

// Two passes rather than one. Erasing and drawing a drop at a time rubs out
// the neighbour that was already drawn where the two overlap, which on a
// screen this small happens constantly.
void paint()
{
	M5GFX &g = ui::gfx();
	g.startWrite();
	for (int i = 0; i < count; i++) {
		const Drop &d = drops[i];
		if (d.drawnX != (int)d.x || d.drawnY != (int)d.y) {
			g.fillRect(d.drawnX, d.drawnY, CELL, CELL, ui::BG);
		}
	}
	for (int i = 0; i < count; i++) {
		Drop &d = drops[i];
		d.drawnX = (int)d.x;
		d.drawnY = (int)d.y;
		ui::glyph(glyphs::CODEPOINTS[d.index], d.drawnX, d.drawnY, d.color);
	}
	drawLabels();
	g.endWrite();
}

void draw()
{
	ui::clearAll();
	for (int i = 0; i < count; i++) {
		drops[i].drawnX = -CELL;
		drops[i].drawnY = -CELL;
	}
	paint();
}

void enter()
{
	seed = millis() | 1u;
	count = START_DROPS;
	fill();
	stepped = millis();
}

void tick()
{
	if (motion::shaken()) {
		// A shake is worth more than a nudge: everything comes off the floor
		// and comes back as different characters.
		for (int i = 0; i < count; i++) {
			deal(drops[i], false);
			drops[i].vx *= 2.5f;
			drops[i].vy *= 2.5f;
		}
	}

	const uint32_t now = millis();
	if (now - stepped < STEP_MS) {
		return;
	}
	stepped = now;
	step();
	paint();
}

bool key(const view::Key &k)
{
	if (k.space && count < MAX_DROPS) {
		deal(drops[count], true);
		count++;
	} else if (k.del && count > 1) {
		count--;
		ui::gfx().fillRect(drops[count].drawnX, drops[count].drawnY, CELL, CELL, ui::BG);
		drawLabels();
		return true;
	} else if (k.enter) {
		fill();
		view::repaint();
		return true;
	} else {
		return false;
	}
	return true;
}

const view::View kRain = {
    .name = "Rain",
    .source = "GLYPHBOTS",
    // Between Calm and Setup. Written out rather than named in view.h, because
    // a view adds itself and never edits the spine.
    .order = 75,
    .icon = icons::CLOUD_RAIN,
    .fullScreen = true,
    .enter = enter,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kRain);
