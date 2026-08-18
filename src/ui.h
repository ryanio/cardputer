#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>

#include "icons.h"

// Drawing on 240x135. Four lines of large text, or eight small ones.
//
// The screen is one shared surface, so the layout is fixed here rather than
// negotiated per view: a title band, a body of eight rows, and a status bar
// along the bottom that only the view loop draws. A view paints inside the
// body and nowhere else.
namespace ui {

constexpr int W = 240;
constexpr int H = 135;

constexpr int STATUS_H = 12;          // the bottom bar, drawn by view::loop
constexpr int BODY_H = H - STATUS_H;  // 123
constexpr int TITLE_H = 18;           // title band, when a view draws one
constexpr int LINE_H = 15;
constexpr int LINES = 7;       // body rows under a title
constexpr int LINES_FULL = 8;  // body rows when a view skips the title

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t BG = rgb565(0, 0, 0);
constexpr uint16_t FG = rgb565(255, 255, 255);
constexpr uint16_t DIM = rgb565(128, 128, 128);
constexpr uint16_t RULE = rgb565(58, 58, 58);
constexpr uint16_t BAR = rgb565(16, 24, 32);
constexpr uint16_t PANEL = rgb565(22, 22, 28);
constexpr uint16_t CORAL = rgb565(255, 127, 80);
constexpr uint16_t GOOD = rgb565(61, 220, 132);
constexpr uint16_t WARN = rgb565(255, 176, 32);
constexpr uint16_t BAD = rgb565(255, 64, 64);

M5GFX &gfx();

// Rotation, brightness, and a cleared screen.
void begin();

// h in degrees, s and l as 0 to 1. glyphbots ships its bot colors as CSS hsl
// strings, so parseHsl takes "hsl(98,20%,8%)" straight from the payload and
// returns false on anything it does not understand.
uint16_t hsl(float h, float s, float l);
bool parseHsl(const char *css, uint16_t &out);

// Everything above the status bar. Also puts row 0 back at the top of the
// screen, so a view that draws no title gets LINES_FULL rows rather than LINES.
void clearBody(uint16_t background = BG);

// The whole panel, for a view that declares fullScreen. Rows and the helpers
// below then run to the bottom edge instead of stopping above the bar.
void clearAll(uint16_t background = BG);

// Drawing a title moves row 0 down by TITLE_H. Nothing else does.
void title(const char *text, uint16_t color = CORAL);

// Row 0 is the first line under whatever the view has drawn so far. Each row
// repaints its own strip, so a view can update one line without clearing the
// screen.
void line(int row, const char *text, uint16_t color = FG, int x = 3);

// Where row 0 sits, and how many rows are left below it.
int contentTop();
int rows();

// Free placement inside the body, for the eight row layout and for anything
// the row grid does not fit.
void lineAt(int y, const char *text, uint16_t color = FG,
            textdatum_t datum = textdatum_t::top_left);

// The one number a view exists to show. Picks the largest font that fits and
// centers it, with an optional unit beside it in small type.
void bigNumber(const char *text, uint16_t color = FG, const char *suffix = nullptr,
               int y = TITLE_H + 4);

// A full width band, for congestion banding behind a number.
void banner(int y, int h, uint16_t color);

// One of the generated Lucide bitmaps, drawn in a single color.
void icon(uint8_t id, int x, int y, uint16_t color);

// The unit the menu and the data views are built from. A screen this small
// wastes less space on a grid of these than on a list with one item per row.
void card(int x, int y, int w, int h, bool selected);

// Centered headline with an optional second line. For empty and error states.
void message(const char *headline, const char *detail = nullptr, uint16_t color = FG);

// Advances one frame per call. For a request that is slow by design, like a
// Coral score.
void spinner(int x, int y, uint16_t color = CORAL);

// Source name on the left, transient note in the middle, radio and battery on
// the right. Only the view loop calls this, which is what keeps the source
// name honest.
void statusBar(const char *source, const char *note = nullptr);

}  // namespace ui
