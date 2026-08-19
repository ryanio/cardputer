#include <math.h>

#include "../motion.h"
#include "../ui.h"
#include "../view.h"

// A marble in a maze, steered by tipping the unit.
//
// The best thing a 240x135 panel and an accelerometer can do together: no
// text, no network, and the only control is the thing in your hand. Three
// mazes, dug fresh every run, so a unit somebody was given is not playing the
// levels somebody else already solved.
//
// A shake shoves the marble, which is what anybody does with a real one when
// it will not come out of a corner, and it costs two seconds for the same
// reason. Shaking it somewhere else is worth trying.
namespace {

// Cells of the maze, then the grid of walls and floors it becomes: a wall
// between every pair, and a wall all the way round.
constexpr int CELLS_X = 9;
constexpr int CELLS_Y = 4;
constexpr int GRID_W = CELLS_X * 2 + 1;  // 19
constexpr int GRID_H = CELLS_Y * 2 + 1;  // 9
constexpr int CELL = 12;
constexpr int BOARD_W = GRID_W * CELL;  // 228
constexpr int BOARD_H = GRID_H * CELL;  // 108
constexpr int X0 = (ui::W - BOARD_W) / 2;
constexpr int Y0 = 13;
constexpr int HUD_Y = 2;

constexpr float RADIUS = 3.5f;
constexpr uint32_t STEP_MS = 16;
// Pixels per step squared at one g, which is a marble that takes a corner
// rather than one that has to be aimed.
constexpr float ACCEL = 0.55f;
constexpr float FRICTION = 0.985f;
constexpr float BOUNCE = 0.35f;
// Below a cell a step, so no speed can carry the marble through a wall.
constexpr float MAX_SPEED = 4.0f;
// A press of an arrow, for a unit with no IMU and for anybody who would rather
// not wave their hands about.
constexpr float NUDGE = 1.6f;

constexpr int LEVELS = 3;
constexpr uint32_t SHAKE_PENALTY_MS = 2000;
constexpr float SHAKE_KICK = 3.2f;

enum class Screen : uint8_t { Title, Play, Won };

Screen screen = Screen::Title;
bool grid[GRID_H][GRID_W];  // true is wall
int level = 1;
bool vault = false;  // the maze nobody was told about

float ballX = 0.0f;
float ballY = 0.0f;
float velX = 0.0f;
float velY = 0.0f;
int drawnX = 0;
int drawnY = 0;

uint32_t startedAt = 0;
uint32_t penalty = 0;
uint32_t runTotal = 0;
int shakes = 0;
uint32_t hudShown = 0;
uint32_t stepped = 0;

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

// Walls carry the whole board, so they are a shade rather than a colour. Coral
// itself at this size is a wall of paint, so the vault gets it darkened and
// keeps the bright one for the marble.
uint16_t wallColor()
{
	return vault ? ui::hsl(14.0f, 0.62f, 0.34f) : ui::hsl(205.0f, 0.30f, 0.42f);
}

uint16_t ballColor()
{
	return vault ? ui::CORAL : ui::FG;
}

float centerOf(int cell)
{
	return (float)(cell * CELL + CELL / 2);
}

bool wallAt(int gx, int gy)
{
	if (gx < 0 || gy < 0 || gx >= GRID_W || gy >= GRID_H) {
		return true;
	}
	return grid[gy][gx];
}

// A backtracker, which is the one maze algorithm that fits in a stack of 36
// cells and never leaves a room unreachable. Walls come down between a cell
// and a neighbour it has not seen, and it backs up when it is boxed in.
void dig()
{
	for (int y = 0; y < GRID_H; y++) {
		for (int x = 0; x < GRID_W; x++) {
			grid[y][x] = true;
		}
	}

	uint8_t stackX[CELLS_X * CELLS_Y];
	uint8_t stackY[CELLS_X * CELLS_Y];
	int depth = 0;
	int cx = 0;
	int cy = 0;
	grid[1][1] = false;
	stackX[depth] = 0;
	stackY[depth] = 0;
	depth++;

	while (depth > 0) {
		cx = stackX[depth - 1];
		cy = stackY[depth - 1];

		int optionX[4];
		int optionY[4];
		int options = 0;
		const int dx[4] = {1, -1, 0, 0};
		const int dy[4] = {0, 0, 1, -1};
		for (int i = 0; i < 4; i++) {
			const int nx = cx + dx[i];
			const int ny = cy + dy[i];
			if (nx < 0 || ny < 0 || nx >= CELLS_X || ny >= CELLS_Y) {
				continue;
			}
			if (!grid[ny * 2 + 1][nx * 2 + 1]) {
				continue;  // already dug
			}
			optionX[options] = nx;
			optionY[options] = ny;
			options++;
		}

		if (options == 0) {
			depth--;
			continue;
		}
		const int chosen = pick(options);
		const int nx = optionX[chosen];
		const int ny = optionY[chosen];
		grid[ny * 2 + 1][nx * 2 + 1] = false;
		grid[cy + ny + 1][cx + nx + 1] = false;  // the wall between the two
		stackX[depth] = (uint8_t)nx;
		stackY[depth] = (uint8_t)ny;
		depth++;
	}

	// A perfect maze has exactly one route, which reads as a corridor with a
	// marble in it. A few walls taken out at random give it loops, and a loop
	// is where tilting beats reading.
	for (int i = 0; i < 4 + level; i++) {
		const int x = 1 + pick(GRID_W - 2);
		const int y = 1 + pick(GRID_H - 2);
		if ((x % 2 == 0) != (y % 2 == 0)) {
			grid[y][x] = false;
		}
	}
}

void startLevel()
{
	dig();
	ballX = (float)X0 + centerOf(1);
	ballY = (float)Y0 + centerOf(1);
	velX = 0.0f;
	velY = 0.0f;
	drawnX = (int)ballX;
	drawnY = (int)ballY;
	startedAt = millis();
	penalty = 0;
}

int goalGridX()
{
	return GRID_W - 2;
}

int goalGridY()
{
	return GRID_H - 2;
}

uint32_t elapsed()
{
	return millis() - startedAt + penalty;
}

// ---------------------------------------------------------------------- draw

void drawCell(int gx, int gy)
{
	M5GFX &g = ui::gfx();
	const int x = X0 + gx * CELL;
	const int y = Y0 + gy * CELL;
	if (wallAt(gx, gy)) {
		g.fillRect(x, y, CELL, CELL, wallColor());
		return;
	}
	g.fillRect(x, y, CELL, CELL, ui::BG);
	if (gx == goalGridX() && gy == goalGridY()) {
		// The hole. A ring rather than a disc, so a marble sitting on it is
		// still a marble on a hole.
		g.drawCircle(x + CELL / 2, y + CELL / 2, 4, ui::GOOD);
		g.drawCircle(x + CELL / 2, y + CELL / 2, 3, ui::GOOD);
	}
}

void drawBoard()
{
	M5GFX &g = ui::gfx();
	g.startWrite();
	for (int y = 0; y < GRID_H; y++) {
		for (int x = 0; x < GRID_W; x++) {
			drawCell(x, y);
		}
	}
	g.endWrite();
}

// Drawn ten times a second, so it paints over itself rather than clearing
// first: a rectangle blanked and refilled at that rate is a flicker across the
// top of the board. Font0 is fixed width and the strings are padded, so the
// old one cannot show around the edges of the new one.
void drawHud()
{
	char text[40];
	M5GFX &g = ui::gfx();
	g.setFont(&fonts::Font0);
	g.setTextDatum(textdatum_t::top_left);
	g.setTextColor(vault ? ui::CORAL : ui::FG, ui::BG);

	const uint32_t ms = elapsed();
	if (vault) {
		snprintf(text, sizeof(text), "the vault   %u.%us  ", (unsigned)(ms / 1000),
		         (unsigned)((ms % 1000) / 100));
	} else {
		snprintf(text, sizeof(text), "maze %d of %d   %u.%us  ", level, LEVELS,
		         (unsigned)(ms / 1000), (unsigned)((ms % 1000) / 100));
	}
	g.drawString(text, 4, HUD_Y);

	if (shakes > 0) {
		snprintf(text, sizeof(text), "%d shake%s  ", shakes, shakes == 1 ? "" : "s");
		g.setTextColor(ui::WARN, ui::BG);
		g.drawString(text, 168, HUD_Y);
	}
	hudShown = millis();
}

void drawBall()
{
	M5GFX &g = ui::gfx();
	const int x = (int)ballX;
	const int y = (int)ballY;
	if (x == drawnX && y == drawnY) {
		return;
	}

	// Put back whatever the marble was sitting on, a cell at a time, then draw
	// it where it is now. Cells are flat colour, so this is exact.
	g.startWrite();
	const int r = (int)RADIUS + 1;
	const int fromX = (drawnX - r - X0) / CELL;
	const int toX = (drawnX + r - X0) / CELL;
	const int fromY = (drawnY - r - Y0) / CELL;
	const int toY = (drawnY + r - Y0) / CELL;
	for (int gy = fromY; gy <= toY; gy++) {
		for (int gx = fromX; gx <= toX; gx++) {
			if (gx >= 0 && gy >= 0 && gx < GRID_W && gy < GRID_H) {
				drawCell(gx, gy);
			}
		}
	}
	g.fillCircle(x, y, (int)RADIUS, ballColor());
	g.endWrite();
	drawnX = x;
	drawnY = y;
}

void drawTitle()
{
	ui::clearAll();
	M5GFX &g = ui::gfx();

	g.setFont(&fonts::Font4);
	g.setTextColor(ui::CORAL, ui::BG);
	g.setTextDatum(textdatum_t::top_center);
	g.drawString("Maze", ui::W / 2, 16);

	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	g.drawString(motion::available() ? "tip the unit and the marble rolls"
	                                 : "no imu here, so the arrows nudge it",
	             ui::W / 2, 48);
	g.drawString("three mazes, dug fresh every time", ui::W / 2, 60);
	g.drawString("stuck in a corner? it rattles", ui::W / 2, 72);

	g.setTextColor(ui::FG, ui::BG);
	g.drawString("enter starts", ui::W / 2, 92);

	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString("esc backs out", 4, ui::H - 9);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString("TILT", ui::W - 4, ui::H - 9);
}

void drawWon()
{
	char text[48];
	ui::clearAll();
	M5GFX &g = ui::gfx();

	g.setFont(&fonts::Font4);
	g.setTextColor(vault ? ui::CORAL : ui::GOOD, ui::BG);
	g.setTextDatum(textdatum_t::top_center);
	g.drawString(vault ? "the vault" : "out", ui::W / 2, 14);

	g.setFont(&fonts::Font2);
	g.setTextColor(ui::FG, ui::BG);
	snprintf(text, sizeof(text), "%u.%us", (unsigned)(runTotal / 1000),
	         (unsigned)((runTotal % 1000) / 100));
	g.drawString(text, ui::W / 2, 46);

	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	if (vault) {
		g.drawString("nothing in here but the rattle", ui::W / 2, 72);
	} else if (shakes == 0) {
		g.drawString("and not one shake", ui::W / 2, 72);
	} else {
		snprintf(text, sizeof(text), "%d shake%s, two seconds each", shakes,
		         shakes == 1 ? "" : "s");
		g.drawString(text, ui::W / 2, 72);
	}
	g.setTextColor(ui::FG, ui::BG);
	g.drawString("enter plays again", ui::W / 2, 92);

	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString("TILT", ui::W - 4, ui::H - 9);
}

void draw()
{
	switch (screen) {
		case Screen::Play:
			ui::clearAll();
			drawBoard();
			drawHud();
			drawnX = -20;
			drawnY = -20;
			drawBall();
			break;
		case Screen::Won:
			drawWon();
			break;
		default:
			drawTitle();
			break;
	}
}

// -------------------------------------------------------------------- physics

void bump()
{
	const float angle = (float)pick(628) / 100.0f;
	velX += cosf(angle) * SHAKE_KICK;
	velY += sinf(angle) * SHAKE_KICK;
	penalty += SHAKE_PENALTY_MS;
	shakes++;
	M5Cardputer.Speaker.tone(180.0f, 60);
	drawHud();
}

void step()
{
	float ax = 0.0f;
	float ay = 0.0f;
	if (motion::available()) {
		ax = motion::gravityX() * ACCEL;
		ay = motion::gravityY() * ACCEL;
	}

	velX = (velX + ax) * FRICTION;
	velY = (velY + ay) * FRICTION;
	velX = constrain(velX, -MAX_SPEED, MAX_SPEED);
	velY = constrain(velY, -MAX_SPEED, MAX_SPEED);

	// One axis at a time, so a marble in a corner slides along the wall it is
	// touching instead of sticking to it.
	ballX += velX;
	const int topRow = (int)(ballY - RADIUS - (float)Y0) / CELL;
	const int bottomRow = (int)(ballY + RADIUS - (float)Y0) / CELL;
	if (velX > 0.0f) {
		const int column = (int)(ballX + RADIUS - (float)X0) / CELL;
		for (int row = topRow; row <= bottomRow; row++) {
			if (wallAt(column, row)) {
				ballX = (float)(X0 + column * CELL) - RADIUS - 0.01f;
				velX = -velX * BOUNCE;
				break;
			}
		}
	} else if (velX < 0.0f) {
		const int column = (int)(ballX - RADIUS - (float)X0) / CELL;
		for (int row = topRow; row <= bottomRow; row++) {
			if (wallAt(column, row)) {
				ballX = (float)(X0 + (column + 1) * CELL) + RADIUS + 0.01f;
				velX = -velX * BOUNCE;
				break;
			}
		}
	}

	ballY += velY;
	const int leftColumn = (int)(ballX - RADIUS - (float)X0) / CELL;
	const int rightColumn = (int)(ballX + RADIUS - (float)X0) / CELL;
	if (velY > 0.0f) {
		const int row = (int)(ballY + RADIUS - (float)Y0) / CELL;
		for (int column = leftColumn; column <= rightColumn; column++) {
			if (wallAt(column, row)) {
				ballY = (float)(Y0 + row * CELL) - RADIUS - 0.01f;
				velY = -velY * BOUNCE;
				break;
			}
		}
	} else if (velY < 0.0f) {
		const int row = (int)(ballY - RADIUS - (float)Y0) / CELL;
		for (int column = leftColumn; column <= rightColumn; column++) {
			if (wallAt(column, row)) {
				ballY = (float)(Y0 + (row + 1) * CELL) + RADIUS + 0.01f;
				velY = -velY * BOUNCE;
				break;
			}
		}
	}
}

bool inTheHole()
{
	const float gx = (float)X0 + centerOf(goalGridX());
	const float gy = (float)Y0 + centerOf(goalGridY());
	const float dx = ballX - gx;
	const float dy = ballY - gy;
	return dx * dx + dy * dy < 16.0f;
}

void finished()
{
	runTotal += elapsed();
	M5Cardputer.Speaker.tone(660.0f, 120);
	if (vault || level >= LEVELS) {
		screen = Screen::Won;
	} else {
		level++;
		startLevel();
	}
	view::repaint();
}

// ------------------------------------------------------------------ lifecycle

void enter()
{
	screen = Screen::Title;
	level = 1;
	vault = false;
	shakes = 0;
	runTotal = 0;
	seed = millis() | 1u;
	M5Cardputer.Speaker.begin();
	M5Cardputer.Speaker.setVolume(80);
}

void leave()
{
	M5Cardputer.Speaker.stop();
}

void begin(bool secret)
{
	vault = secret;
	level = 1;
	shakes = 0;
	runTotal = 0;
	screen = Screen::Play;
	startLevel();
	if (secret) {
		// It is a maze like the others. Finding it is the prize, and the HUD
		// names it rather than leaving anybody wondering what they did.
		M5Cardputer.Speaker.tone(523.0f, 90);
	}
	view::repaint();
}

void tick()
{
	if (motion::shaken()) {
		if (screen == Screen::Play) {
			bump();
		} else {
			// Shaking a menu is the kind of thing somebody does once, which is
			// exactly why this is what it opens.
			begin(true);
			return;
		}
	}

	if (screen != Screen::Play) {
		return;
	}

	const uint32_t now = millis();
	if (now - stepped < STEP_MS) {
		return;
	}
	stepped = now;

	step();
	drawBall();
	if (now - hudShown > 100) {
		drawHud();
	}
	if (inTheHole()) {
		finished();
	}
}

bool key(const view::Key &k)
{
	if (screen != Screen::Play) {
		if (k.enter || k.space) {
			begin(false);
			return true;
		}
		return false;
	}

	// The arrows are the fallback, and they are a shove rather than a hold,
	// because a key that reports once on the way down cannot be leaned on.
	if (k.left) {
		velX -= NUDGE;
	} else if (k.right) {
		velX += NUDGE;
	} else if (k.up) {
		velY -= NUDGE;
	} else if (k.down) {
		velY += NUDGE;
	} else if (k.enter) {
		startLevel();
		view::repaint();
	} else {
		return false;
	}
	return true;
}

const view::View kMaze = {
    .name = "Maze",
    .source = "TILT",
    // Between Rain and Setup. Written out rather than named in view.h, because
    // a view adds itself and never edits the spine.
    .order = 80,
    .icon = icons::ROUTE,
    .fullScreen = true,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kMaze);
