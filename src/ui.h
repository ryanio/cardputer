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

// One atlas cell. Four lines of these fill 128 of the 135 rows, which is what
// makes a bot fit. Stated here rather than pulled from glyphs.h so that the
// 13KB atlas lands in one translation unit; ui.cpp asserts the two agree.
constexpr int GLYPH_CELL = 32;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// The ten colours the whole panel is drawn from, as one struct. A palette is
// set whole rather than a colour at a time, because a build that set four of
// them would be a screen half in somebody's scheme and half in flint's, which
// looks like a bug in whichever half you were not expecting.
//
// Default constructed it is flint's own scheme, so a caller that wants one
// colour changed starts from `ui::Palette p;`, or from palette() to keep what
// is on screen now, and edits the field it cares about.
struct Palette {
	uint16_t bg = rgb565(0, 0, 0);
	uint16_t fg = rgb565(255, 255, 255);
	uint16_t dim = rgb565(128, 128, 128);
	uint16_t rule = rgb565(58, 58, 58);
	uint16_t bar = rgb565(16, 24, 32);    // the status bar's ground
	uint16_t panel = rgb565(22, 22, 28);  // a card nobody has selected
	// Drawn as ui::CORAL: the selected card, a title, a spinner. The field is
	// named for the job rather than for the colour, because an app pack setting
	// a scheme of its own is not shipping a coral.
	uint16_t accent = rgb565(255, 127, 80);
	uint16_t good = rgb565(61, 220, 132);
	uint16_t warn = rgb565(255, 176, 32);
	uint16_t bad = rgb565(255, 64, 64);
};

// The same ten under the names every view already draws with. They are
// variables rather than constants so that setPalette can move them, and reading
// one is a load rather than a call, which is what lets a view keep them inside
// a per pixel loop the way they were used when they were constexpr.
//
// A build that never calls setPalette holds exactly the values above, so flint
// on its own draws what it always drew.
extern uint16_t BG;
extern uint16_t FG;
extern uint16_t DIM;
extern uint16_t RULE;
extern uint16_t BAR;
extern uint16_t PANEL;
extern uint16_t CORAL;  // Palette::accent
extern uint16_t GOOD;
extern uint16_t WARN;
extern uint16_t BAD;

// What those ten hold right now.
Palette palette();

// Point them somewhere else. The menu, the status bar and every view follow,
// because they all read the names above rather than colours of their own.
//
// This is a seam, like flint.ini and view::appBegin: where an app pack got a
// palette, and whether it came off a cable, a settings screen or a constant in
// its own tree, is not flint's business and nothing here asks. See
// docs/APPS.md.
//
// Asks for a repaint, because a palette set while a view is on screen otherwise
// reaches only the parts something else happens to redraw, and a screen in two
// schemes at once reads as a seam that half works. A palette equal to the
// current one is dropped rather than drawn: an app fed its theme by a transport
// that repeats it in every message would otherwise ask for a repaint every
// message, which is a flicker rather than a theme.
void setPalette(const Palette &p);

M5GFX &gfx();

// Rotation, brightness, and a cleared screen.
void begin();

// h in degrees, s and l as 0 to 1. glyphbots ships its bot colors as CSS hsl
// strings, so parseHsl takes "hsl(98,20%,8%)" straight from the payload and
// returns false on anything it does not understand.
uint16_t hsl(float h, float s, float l);
bool parseHsl(const char *css, uint16_t &out);

// A bot's colors arrive as CSS, and the collection uses both forms: two of
// every three bots sampled came back as #rrggbb rather than hsl(). This takes
// either, plus the three digit hex shorthand.
bool parseColor(const char *css, uint16_t &out);

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

// Text inside a box: drawn at x and y with that datum, trimmed to maxWidth and
// ended with a period when it does not fit.
//
// The font is whatever the caller last set, and the colors are given rather
// than assumed, which is what the row helpers above cannot do: they draw in
// Font2 on the black ground. Anything painting inside a card of its own needs
// this instead, and every view that drew one had been writing its own copy of
// the same trimming loop.
void clip(const char *text, int x, int y, int maxWidth, uint16_t color, uint16_t background,
          textdatum_t datum = textdatum_t::top_left);

// The one number a view exists to show. Picks the largest font that fits and
// centers it, with an optional unit beside it in small type.
void bigNumber(const char *text, uint16_t color = FG, const char *suffix = nullptr,
               int y = TITLE_H + 4);

// A full width band, for congestion banding behind a number.
void banner(int y, int h, uint16_t color);

// One of the generated Lucide bitmaps, drawn in a single color.
void icon(uint8_t id, int x, int y, uint16_t color);

// The same, for art an app brought with it. An app that is not in this tree
// has no id in the generated atlas, so it hands over the bitmap instead.
void icon(const icons::Icon &art, int x, int y, uint16_t color);

// One glyph from the generated atlas, drawn in a single color at its top left
// corner. Returns false when the collection has a character the atlas does not,
// which means the atlas needs regenerating rather than the view working around
// it. See tools/glyphs/generate.py.
bool glyph(uint32_t codepoint, int x, int y, uint16_t color);

// Decodes one UTF-8 character and advances the pointer past it. Bot art is
// UTF-8 and every character in it is one atlas cell wide.
uint32_t nextCodepoint(const char *&text);

// The unit the menu and the data views are built from. A screen this small
// wastes less space on a grid of these than on a list with one item per row.
void card(int x, int y, int w, int h, bool selected);

// Centered headline with an optional second line. For empty and error states.
void message(const char *headline, const char *detail = nullptr, uint16_t color = FG);

// Font0, six pixels a character, for what the row grid is too coarse to hold:
// a caveat, a headline, a bullet. Cut and marked at the right edge like every
// other string this file draws.
void small(int x, int y, const char *text, uint16_t color);

// Break text into lines of at most `chars`, on whitespace, and return how many
// were written. A word longer than the line runs off rather than being broken.
constexpr int WRAP_MAX = 44;
int wrap(const char *text, int chars, char lines[][WRAP_MAX], int maxLines);

// Dollars in the width a row can spare: $2.66M, $997.2K, $412. Zero and
// negative read as unknown, because that is what a missing number means here.
void usd(float value, char *out, size_t n);

// Trim to a pixel budget in Font2, so a name cannot run underneath the number
// sitting to its right.
void fit(const char *text, int budget, char *out, size_t n);

// The panel's fonts are ASCII. Sources write middle dots, dashes and curly
// quotes, which arrive as UTF-8 and would draw as rubble, so anything above
// ASCII is folded down or dropped. A middle dot becomes a bar, because it is a
// separator and the views that draw one read it as such, and an em or en dash
// becomes a hyphen: Anchor writes one where it has no reading, and dropping it
// would turn "no answer" into an empty space, which is a different claim.
void asciify(const char *src, char *out, size_t n);

// Advances one frame per call. For a request that is slow by design, like a
// Coral score.
void spinner(int x, int y, uint16_t color = CORAL, uint16_t background = BG);

// Source name on the left, transient note in the middle, radio and battery on
// the right. Only the view loop calls this, which is what keeps the source
// name honest.
void statusBar(const char *source, const char *note = nullptr);

}  // namespace ui
