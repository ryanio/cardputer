#include "../ui.h"
#include "../view.h"

// An eight step drum machine for people who do not play anything. It starts
// with a beat already in it, so the first thing that happens is a sound rather
// than a blank grid. Keys 1 to 8 turn steps on and off, up and down pick the
// track, space starts and stops.
namespace {

constexpr int STEPS = 8;
constexpr int TRACKS = 4;

struct Track {
	const char *name;
	float hz;  // the speaker only does tones, so a drum is a short one
	uint16_t ms;
	// A small speaker is loudest in the middle of its range and nearly deaf at
	// the bottom of it, so a hat at 1.5kHz buries a kick at 70Hz unless the mix
	// says otherwise. Channel volume is squared before it is applied, so the hat
	// at 88 lands near an eighth of the amplitude of a kick at 255, not a third.
	uint8_t volume;
};

const Track TRACKS_DEF[TRACKS] = {
    {"KICK", 70.0f, 90, 255},
    {"SNARE", 220.0f, 60, 200},
    {"HAT", 1500.0f, 25, 88},
    {"BASS", 110.0f, 120, 235},
};

// Something you would nod to, so the view makes sense before anyone reads a
// hint. Kick on the ones, hat on the offbeats, snare answering.
bool grid[TRACKS][STEPS] = {
    {true, false, false, false, true, false, false, false},
    {false, false, true, false, false, false, true, false},
    {true, false, true, false, true, false, true, true},
    {true, false, false, true, false, false, true, false},
};

constexpr int BPM_MIN = 60;
constexpr int BPM_MAX = 180;

int bpm = 100;
int track = 0;
int step = 0;
bool playing = false;
uint32_t stepAt = 0;

constexpr int GRID_X = 52;
constexpr int GRID_Y = 26;
constexpr int CELL_W = 22;
constexpr int CELL_H = 20;
constexpr int GAP = 2;

uint16_t trackColor(int i)
{
	switch (i) {
		case 0:
			return ui::CORAL;
		case 1:
			return ui::hsl(48.0f, 0.9f, 0.6f);
		case 2:
			return ui::hsl(180.0f, 0.6f, 0.62f);
		default:
			return ui::hsl(280.0f, 0.55f, 0.66f);
	}
}

uint32_t stepMs()
{
	// Eighth notes, so a step is half a beat.
	return (uint32_t)(60000 / bpm / 2);
}

void drawCell(int t, int s)
{
	M5GFX &g = ui::gfx();
	const int x = GRID_X + s * (CELL_W + GAP);
	const int y = GRID_Y + t * (CELL_H + GAP);
	const bool on = grid[t][s];
	const bool here = playing && s == step;
	const bool cursor = t == track;

	uint16_t fill = on ? trackColor(t) : ui::PANEL;
	if (here) {
		fill = on ? ui::FG : ui::RULE;
	}
	g.fillRoundRect(x, y, CELL_W, CELL_H, 3, fill);
	if (cursor && !on) {
		g.drawRoundRect(x, y, CELL_W, CELL_H, 3, ui::DIM);
	}
}

void drawGrid()
{
	for (int t = 0; t < TRACKS; t++) {
		for (int s = 0; s < STEPS; s++) {
			drawCell(t, s);
		}
	}
}

void drawChrome()
{
	char text[40];
	M5GFX &g = ui::gfx();

	g.fillRect(0, 0, ui::W, GRID_Y, ui::BG);
	g.setFont(&fonts::Font2);
	g.setTextColor(ui::FG, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString("Beat", 4, 2);

	g.setFont(&fonts::Font0);
	g.setTextDatum(textdatum_t::top_right);
	// The arrows either side of the number are the control: those two keys
	// print , and . on their caps, so naming the characters taught nobody
	// anything. Showing them where the number is does.
	snprintf(text, sizeof(text), "< %d bpm >  %s", bpm, playing ? "playing" : "stopped");
	g.setTextColor(playing ? ui::CORAL : ui::DIM, ui::BG);
	g.drawString(text, ui::W - 4, 6);

	// Step numbers over the grid, so 1 to 8 is obvious without a legend.
	g.setTextColor(ui::RULE, ui::BG);
	g.setTextDatum(textdatum_t::top_center);
	for (int s = 0; s < STEPS; s++) {
		snprintf(text, sizeof(text), "%d", s + 1);
		g.drawString(text, GRID_X + s * (CELL_W + GAP) + CELL_W / 2, GRID_Y - 9);
	}

	for (int t = 0; t < TRACKS; t++) {
		const int y = GRID_Y + t * (CELL_H + GAP);
		g.fillRect(0, y, GRID_X - 4, CELL_H, ui::BG);
		g.setFont(&fonts::Font0);
		g.setTextDatum(textdatum_t::top_left);
		g.setTextColor(t == track ? trackColor(t) : ui::DIM, ui::BG);
		g.drawString(TRACKS_DEF[t].name, 4, y + 6);
	}

	g.fillRect(0, ui::H - 11, ui::W, 11, ui::BG);
	g.setFont(&fonts::Font0);
	g.setTextDatum(textdatum_t::top_left);
	g.setTextColor(ui::DIM, ui::BG);
	g.drawString("1-8 steps  space plays  ^v track", 4, ui::H - 9);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString("BEAT", ui::W - 4, ui::H - 9);
}

void draw()
{
	ui::clearAll();
	drawChrome();
	drawGrid();
}

// One track to one of the speaker's virtual channels, which is what lets a
// step play a kick and a hat at once instead of whichever of them happens to
// be longer. Stopping the current sound is scoped to the channel, so a
// retrigger cuts its own track and nothing else.
void hit(int t)
{
	M5Cardputer.Speaker.tone(TRACKS_DEF[t].hz, TRACKS_DEF[t].ms, t, true);
}

void enter()
{
	playing = false;
	step = 0;
	M5Cardputer.Speaker.begin();
	// Four channels can sum past full scale, so the master leaves them room.
	M5Cardputer.Speaker.setVolume(100);
	for (int t = 0; t < TRACKS; t++) {
		M5Cardputer.Speaker.setChannelVolume(t, TRACKS_DEF[t].volume);
	}
}

void leave()
{
	playing = false;
	M5Cardputer.Speaker.stop();
}

void tick()
{
	if (!playing || millis() - stepAt < stepMs()) {
		return;
	}
	stepAt = millis();
	const int previous = step;
	step = (step + 1) % STEPS;

	// Everything on this step sounds, each on its own channel.
	for (int t = 0; t < TRACKS; t++) {
		if (grid[t][step]) {
			hit(t);
		}
	}

	// Only the two columns that changed get repainted.
	for (int t = 0; t < TRACKS; t++) {
		drawCell(t, previous);
		drawCell(t, step);
	}
}

bool key(const view::Key &k)
{
	if (k.ch >= '1' && k.ch <= '8') {
		const int s = k.ch - '1';
		grid[track][s] = !grid[track][s];
		if (grid[track][s]) {
			hit(track);
		}
		drawCell(track, s);
		return true;
	}
	if (k.space) {
		playing = !playing;
		step = 0;
		stepAt = millis();
		view::repaint();
		return true;
	}
	if (k.up || k.down) {
		track = k.up ? (track + TRACKS - 1) % TRACKS : (track + 1) % TRACKS;
		view::repaint();
		return true;
	}
	// The arrow keys print , and . so tempo lives on the keys next to them.
	if (k.left || k.right) {
		bpm = constrain(bpm + (k.right ? 5 : -5), BPM_MIN, BPM_MAX);
		drawChrome();
		return true;
	}
	if (k.del) {
		for (int t = 0; t < TRACKS; t++) {
			for (int s = 0; s < STEPS; s++) {
				grid[t][s] = false;
			}
		}
		view::repaint();
		return true;
	}
	return false;
}

const view::View kBeat = {
    .name = "Beat",
    .source = "SPEAKER",
    .order = view::ORDER_BEAT,
    .icon = icons::MUSIC,
    .fullScreen = true,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kBeat);
