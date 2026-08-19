#include "app.h"

#include <M5Cardputer.h>

#include <string>
#include <vector>

#include "motion.h"
#include "net.h"
#include "net_sim.h"
#include "rest.h"
#include "store.h"
#include "ui.h"
#include "version.h"
#include "view.h"

// The simulator runs the firmware's own view, ui and store code against
// M5GFX's desktop panel. What is faked is the ring around it: keys come from
// SDL, the battery is a number we can set, and the network answers from
// net_sim.cpp. Anything you see here about layout, navigation or the exit
// convention is true of the device.
//
// Keys: the host keyboard is the Cardputer's. Arrows are the four keys that
// print arrows, Escape is the backtick, so Escape always backs out. Alt is Fn.
// Home or F1 is the G0 button.
//
// The mouse is the IMU. Where the pointer sits in the window is how far the
// unit is tipped, holding the left button shakes it, and F2 turns it face
// down, which is what puts the panel to sleep.

namespace {

// Frame capture, which is what makes this a test harness rather than a toy:
// drive keys, write what the panel holds after each one, and an agent can see
// the result of a UI change without anyone looking at a window.
void capture(const std::string &path)
{
	// readRectRGB hands back plain 24 bit triplets. Reading 565 and unpacking
	// by hand means guessing at the panel's byte order, and guessing wrong
	// makes a screenshot that lies about the colors.
	static std::vector<uint8_t> pixels(ui::W * ui::H * 3);
	M5Cardputer.Display.readRectRGB(0, 0, ui::W, ui::H, pixels.data());

	FILE *out = fopen(path.c_str(), "wb");
	if (out == nullptr) {
		Serial.printf("sim: could not write %s\n", path.c_str());
		return;
	}
	fprintf(out, "P6\n%d %d\n255\n", ui::W, ui::H);
	fwrite(pixels.data(), 1, pixels.size(), out);
	fclose(out);
	Serial.printf("sim: wrote %s\n", path.c_str());
}

// A tour so nobody has to guess what to press. Each step is a delay, a key,
// and the reason, which prints so a watcher can follow along.
struct Step {
	uint32_t after;  // ms since the tour started
	const char *key;
	const char *why;
};

const Step TOUR[] = {
    {2600, ".", "move down the menu"},
    {3400, ".", "and again"},
    {4200, "\n", "open Reef"},
    {6200, "`", "backtick backs out of anything"},
    {7200, "8", "jump straight to Setup"},
    {8600, "\n", "scan for networks"},
    {11000, ".", "pick the second one"},
    {12000, "\n", "it is open, so it joins without a passphrase"},
    {15500, "`", "back to the menu"},
};
constexpr int TOUR_STEPS = sizeof(TOUR) / sizeof(TOUR[0]);

bool tourRunning = false;
uint32_t tourStarted = 0;
int tourNext = 0;
const char *lastHint = nullptr;

std::string script;      // keys to play, one every SCRIPT_GAP
std::string shotPrefix;  // where frames go, empty for none
uint32_t quitAfter = 0;  // ms, 0 for never
size_t scriptNext = 0;
int shotIndex = 0;
uint32_t scriptAt = 700;
constexpr uint32_t SCRIPT_GAP = 420;

void runScript()
{
	if (scriptNext > script.size() || millis() < scriptAt) {
		return;
	}
	scriptAt = millis() + SCRIPT_GAP;

	if (!shotPrefix.empty()) {
		char path[256];
		snprintf(path, sizeof(path), "%s-%02d.ppm", shotPrefix.c_str(), shotIndex++);
		capture(path);
	}
	if (scriptNext < script.size()) {
		const char key = script[scriptNext];
		Serial.printf("  key   %s\n", key == '\n' ? "return" : std::string(1, key).c_str());
		M5Cardputer.Keyboard.queue(key);
	}
	scriptNext++;
}

void hint()
{
	const view::View *v = view::active();
	const char *text = v == nullptr ? "menu: arrows move, 1 to 5 jump, enter opens"
	                                : "in a view: escape or the backtick goes back to the menu";
	if (text != lastHint) {
		lastHint = text;
		Serial.printf("\n  hint  %s\n", text);
	}
}

void runTour()
{
	if (!tourRunning || tourNext >= TOUR_STEPS) {
		return;
	}
	const Step &step = TOUR[tourNext];
	if (millis() - tourStarted < step.after) {
		return;
	}
	Serial.printf("  tour  %-4s %s\n", step.key[0] == '\n' ? "ret" : step.key, step.why);
	M5Cardputer.Keyboard.queue(step.key[0]);
	tourNext++;
}

}  // namespace

void simSetup()
{
	auto cfg = M5.config();
	M5Cardputer.begin(cfg, true);

	ui::begin();
	store::begin();

	Serial.printf("\nflint %s in the simulator\n", FW_VERSION);
	Serial.println("keys: arrows move, enter opens, escape backs out, alt is fn");
	Serial.println("      home or F1 is the G0 button");
	Serial.println("imu:  the mouse tilts it, the left button shakes it, F2 is face down\n");

	net::begin();
	view::begin();
	view::repaint();
}

void simLoop()
{
	M5Cardputer.update();
	motion::update();
	rest::loop(M5Cardputer.Keyboard.isChange());
	net::loop();
	runTour();
	runScript();
	view::loop();
	hint();
	delay(5);

	if (quitAfter != 0 && millis() > quitAfter) {
		Serial.println("sim: done");
		exit(0);
	}
}

void simArgs(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const bool more = i + 1 < argc;
		if (strcmp(argv[i], "--tour") == 0) {
			tourRunning = true;
		} else if (strcmp(argv[i], "--keys") == 0 && more) {
			// \n is enter, \b is backspace and \t is tab, so a whole session
			// fits in one argument: --keys "5\n\n.\n"
			const std::string raw = argv[++i];
			for (size_t c = 0; c < raw.size(); c++) {
				if (raw[c] == '\\' && c + 1 < raw.size()) {
					const char next = raw[++c];
					script += next == 'n' ? '\n' : next == 'b' ? '\b' : next == 't' ? '\t' : next;
				} else {
					script += raw[c];
				}
			}
		} else if (strcmp(argv[i], "--shot") == 0 && more) {
			shotPrefix = argv[++i];
		} else if (strcmp(argv[i], "--quit-after") == 0 && more) {
			quitAfter = (uint32_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--tilt") == 0 && more) {
			// --tilt 0.6,0.4 pins the lean, so a screenshot of a motion view
			// does not depend on where somebody left the pointer. Right and
			// down are positive, and 1 is the whole way over.
			float x = 0.0f;
			float y = 0.0f;
			if (sscanf(argv[++i], "%f,%f", &x, &y) == 2) {
				M5.Imu.setTilt(x, y);
			}
		} else if (strcmp(argv[i], "--orbit") == 0 && more) {
			// --orbit 5 rolls the lean all the way round every five seconds,
			// which keeps anything that falls or rolls moving on its own.
			M5.Imu.setOrbit(strtof(argv[++i], nullptr));
		} else if (strcmp(argv[i], "--shake") == 0) {
			M5.Imu.setShaking(true);
		} else if (strcmp(argv[i], "--latency") == 0 && more) {
			// Fixtures answer instantly, which hides the screen a view draws
			// while it waits. Coral's score takes seconds on a real unit.
			net::simLatency((uint32_t)strtoul(argv[++i], nullptr, 10));
		}
	}
	tourStarted = millis();
}

extern "C" void simPress(int key)
{
	M5Cardputer.Keyboard.queue((char)key);
}
