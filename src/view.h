#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>

#include "icons.h"

// The menu, the input loop, and the rule that every screen has a way out.
//
// A view registers itself from its own file with VIEW_REGISTER, so adding one
// never means editing a shared list. Menu position comes from the order field.
//
// The exit convention is enforced here, not in the views: the loop takes esc,
// which is the backtick key, and the G0 button before a view sees them and
// returns to the menu. A view cannot trap anyone by forgetting to handle a
// key. Fn and esc together reach the view as a plain backtick, for text entry.
//
// Hints on screen say esc rather than showing the character, because Font2
// draws a backtick as a small raised circle that reads as a degree sign.
//
// The loop also draws the status bar, using the active view's source name. A
// view never draws over the bottom 13 pixels.
namespace view {

// Frozen so parallel work agrees on menu order without coordinating.
enum Order : int {
	ORDER_REEF = 10,
	ORDER_BANKR = 20,
	ORDER_BOT = 30,
	ORDER_WOMP = 40,
	ORDER_GAS = 50,
	ORDER_BEAT = 60,
	ORDER_CALM = 70,
	ORDER_LAST = 90,
};

struct Key {
	char ch = 0;  // the printable character, 0 when there is none

	// The four keys with arrows printed on them. They report as both the
	// character and the direction, so a view that reads text can use ch and
	// ignore the arrows.
	bool up = false;
	bool down = false;
	bool left = false;
	bool right = false;

	bool enter = false;
	bool del = false;
	bool space = false;
	bool tab = false;
	bool fn = false;
	bool shift = false;
	bool ctrl = false;
	bool opt = false;
	bool alt = false;
};

struct View {
	const char *name;    // menu label: Gas
	const char *source;  // status bar source: GWEI, GLYPHBOTS, CORAL, VOXELS
	int order;
	uint8_t icon = icons::COUNT;  // drawn on the menu card, COUNT for none

	// A view whose content is the whole point, a picture or a bot, takes the
	// panel and the loop draws no status bar. It then owns the rule that a
	// screen names its source, and paints the name over its own corner.
	bool fullScreen = false;

	void (*enter)() = nullptr;  // opened
	void (*leave)() = nullptr;  // closed, so drop anything held
	void (*draw)() = nullptr;   // repaint the body, never the status bar
	void (*tick)() = nullptr;   // every loop while active: polls live here
	// Return true when the view consumed the key. The loop has no fallback
	// today, so the value is only there for a view that wraps another one.
	bool (*key)(const Key &) = nullptr;
};

void add(const View *v);
int count();
const View *at(int index);

struct Registrar {
	explicit Registrar(const View *v)
	{
		add(v);
	}
};

#define VIEW_REGISTER(v) static const view::Registrar view_registrar_##v(&(v))

// A view is one file that ends like this:
//
//   namespace {
//   void draw() { ui::clearBody(); ui::title("Gas"); }
//   const view::View kGas = {
//       .name = "Gas",
//       .source = "GWEI",
//       .order = view::ORDER_GAS,
//       .draw = draw,
//   };
//   }  // namespace
//   VIEW_REGISTER(kGas);

void begin();
void loop();

void open(int index);
void back();
bool inMenu();
const View *active();

// Ask for a body repaint on the next loop. Cheap to call, so call it whenever
// the data behind a view changes.
void repaint();

// A short lived line in the status bar: fetching, saved, no wifi.
void note(const char *text, uint32_t ms = 4000);

}  // namespace view
