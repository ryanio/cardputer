#include "ui.h"

#include <ctype.h>
#include <math.h>

#include "net.h"

namespace ui {

namespace {

constexpr uint8_t BRIGHTNESS = 110;

uint8_t spinnerFrame = 0;

// Row 0 sits at the top of the screen until a title moves it down, and the
// body stops above the status bar unless the view owns the whole panel.
int contentTop_ = 0;
int contentBottom_ = BODY_H;

int rowY(int row)
{
	return contentTop_ + row * LINE_H;
}

// Fits text into a width by dropping characters and ending in a period, which
// reads better than a hard cut on a screen this narrow.
void drawClipped(const char *text, int x, int y, int maxWidth, uint16_t color, uint16_t background,
                 textdatum_t datum)
{
	M5GFX &g = gfx();
	g.setTextColor(color, background);
	g.setTextDatum(datum);
	if (g.textWidth(text) <= maxWidth) {
		g.drawString(text, x, y);
		return;
	}
	String cut(text);
	while (cut.length() > 1 && g.textWidth((cut + ".").c_str()) > maxWidth) {
		cut.remove(cut.length() - 1);
	}
	cut += ".";
	g.drawString(cut.c_str(), x, y);
}

uint16_t radioColor()
{
	switch (net::state()) {
		case net::Wifi::Online:
			return GOOD;
		case net::Wifi::Joining:
			return WARN;
		case net::Wifi::Failed:
			return BAD;
		default:
			return DIM;
	}
}

}  // namespace

M5GFX &gfx()
{
	return M5Cardputer.Display;
}

void begin()
{
	M5GFX &g = gfx();
	g.setRotation(1);
	g.setBrightness(BRIGHTNESS);
	g.setTextWrap(false);
	g.setTextSize(1);
	g.fillScreen(BG);
}

uint16_t hsl(float h, float s, float l)
{
	h = fmodf(h, 360.0f);
	if (h < 0.0f) {
		h += 360.0f;
	}
	s = constrain(s, 0.0f, 1.0f);
	l = constrain(l, 0.0f, 1.0f);

	const float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
	const float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
	const float m = l - c / 2.0f;

	float r = 0.0f, g = 0.0f, b = 0.0f;
	if (h < 60.0f) {
		r = c;
		g = x;
	} else if (h < 120.0f) {
		r = x;
		g = c;
	} else if (h < 180.0f) {
		g = c;
		b = x;
	} else if (h < 240.0f) {
		g = x;
		b = c;
	} else if (h < 300.0f) {
		r = x;
		b = c;
	} else {
		r = c;
		b = x;
	}

	return rgb565((uint8_t)lroundf((r + m) * 255.0f), (uint8_t)lroundf((g + m) * 255.0f),
	              (uint8_t)lroundf((b + m) * 255.0f));
}

bool parseHsl(const char *css, uint16_t &out)
{
	if (css == nullptr) {
		return false;
	}
	const char *p = strchr(css, '(');
	if (p == nullptr) {
		return false;
	}
	p++;

	float v[3] = {0.0f, 0.0f, 0.0f};
	int n = 0;
	while (*p != '\0' && n < 3) {
		while (*p == ' ' || *p == ',' || *p == '/') {
			p++;
		}
		char *end = nullptr;
		const float f = strtof(p, &end);
		if (end == p) {
			break;
		}
		v[n++] = f;
		p = end;
		// Skip the unit: a percent sign, or deg / rad / turn on the hue.
		while (*p == '%' || isalpha((unsigned char)*p)) {
			p++;
		}
	}
	if (n < 3) {
		return false;
	}
	out = hsl(v[0], v[1] / 100.0f, v[2] / 100.0f);
	return true;
}

void clearBody(uint16_t background)
{
	contentTop_ = 0;
	contentBottom_ = BODY_H;
	gfx().fillRect(0, 0, W, BODY_H, background);
}

void clearAll(uint16_t background)
{
	contentTop_ = 0;
	contentBottom_ = H;
	gfx().fillScreen(background);
}

int contentTop()
{
	return contentTop_;
}

int rows()
{
	return (contentBottom_ - contentTop_) / LINE_H;
}

void title(const char *text, uint16_t color)
{
	M5GFX &g = gfx();
	g.fillRect(0, 0, W, TITLE_H, BG);
	g.setFont(&fonts::Font2);
	drawClipped(text, 3, 0, W - 6, color, BG, textdatum_t::top_left);
	// A descender in the title reaches the row above TITLE_H, so the rule sits
	// one lower than it looks like it should: at TITLE_H - 2 the tail of a p or
	// a g lands on the line.
	g.drawFastHLine(0, TITLE_H - 1, W, RULE);
	contentTop_ = TITLE_H;
}

void line(int row, const char *text, uint16_t color, int x)
{
	if (row < 0) {
		return;
	}
	const int y = rowY(row);
	if (y + LINE_H > contentBottom_) {
		return;
	}
	M5GFX &g = gfx();
	g.fillRect(0, y, W, LINE_H, BG);
	g.setFont(&fonts::Font2);
	drawClipped(text, x, y, W - x - 3, color, BG, textdatum_t::top_left);
}

void lineAt(int y, const char *text, uint16_t color, textdatum_t datum)
{
	M5GFX &g = gfx();
	g.setFont(&fonts::Font2);
	const int x = datum == textdatum_t::top_center ? W / 2 : (datum == textdatum_t::top_right ? W - 3 : 3);
	drawClipped(text, x, y, W - 6, color, BG, datum);
}

void bigNumber(const char *text, uint16_t color, const char *suffix, int y)
{
	M5GFX &g = gfx();
	const lgfx::IFont *fonts_by_size[] = {&fonts::DejaVu40, &fonts::DejaVu24, &fonts::Font2};

	const int suffixWidth = [&]() {
		if (suffix == nullptr) {
			return 0;
		}
		g.setFont(&fonts::Font2);
		return (int)g.textWidth(suffix) + 6;
	}();

	const lgfx::IFont *chosen = fonts_by_size[2];
	int numberWidth = 0;
	for (const lgfx::IFont *f : fonts_by_size) {
		g.setFont(f);
		numberWidth = g.textWidth(text);
		if (numberWidth + suffixWidth <= W - 8) {
			chosen = f;
			break;
		}
	}

	g.setFont(chosen);
	const int height = g.fontHeight();
	g.fillRect(0, y, W, min(height + 4, contentBottom_ - y), BG);

	const int left = (W - (numberWidth + suffixWidth)) / 2;
	g.setTextColor(color, BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString(text, left, y);

	if (suffix != nullptr) {
		g.setFont(&fonts::Font2);
		g.setTextColor(DIM, BG);
		g.drawString(suffix, left + numberWidth + 6, y + height - g.fontHeight() - 2);
	}
}

void banner(int y, int h, uint16_t color)
{
	if (y >= contentBottom_) {
		return;
	}
	gfx().fillRect(0, y, W, min(h, contentBottom_ - y), color);
}

void icon(uint8_t id, int x, int y, uint16_t color)
{
	if (id >= icons::COUNT) {
		return;
	}
	const icons::Icon &glyph = icons::ALL[id];
	gfx().drawBitmap(x, y, glyph.data, glyph.width, glyph.height, color);
}

void card(int x, int y, int w, int h, bool selected)
{
	M5GFX &g = gfx();
	g.fillRoundRect(x, y, w, h, 4, selected ? CORAL : PANEL);
	if (!selected) {
		g.drawRoundRect(x, y, w, h, 4, RULE);
	}
}

void message(const char *headline, const char *detail, uint16_t color)
{
	M5GFX &g = gfx();
	clearBody();
	g.setFont(&fonts::Font2);
	drawClipped(headline, W / 2, contentBottom_ / 2 - (detail == nullptr ? 8 : 16), W - 8, color, BG,
	            textdatum_t::top_center);
	if (detail != nullptr) {
		g.setFont(&fonts::Font0);
		drawClipped(detail, W / 2, contentBottom_ / 2 + 6, W - 8, DIM, BG, textdatum_t::top_center);
	}
}

void spinner(int x, int y, uint16_t color)
{
	M5GFX &g = gfx();
	constexpr int dots = 8;
	constexpr int radius = 7;
	for (int i = 0; i < dots; i++) {
		const float a = (float)i * (2.0f * PI / dots);
		const int px = x + (int)lroundf(cosf(a) * radius);
		const int py = y + (int)lroundf(sinf(a) * radius);
		const bool lit = i == (spinnerFrame % dots);
		g.fillCircle(px, py, lit ? 2 : 1, lit ? color : RULE);
	}
	spinnerFrame++;
}

void statusBar(const char *source, const char *note)
{
	M5GFX &g = gfx();
	const int top = H - STATUS_H;
	g.fillRect(0, top, W, STATUS_H, BAR);
	g.drawFastHLine(0, top, W, RULE);
	g.setFont(&fonts::Font0);
	g.setTextSize(1);

	if (source != nullptr) {
		g.setTextColor(DIM, BAR);
		g.setTextDatum(textdatum_t::top_left);
		g.drawString(source, 3, top + 3);
	}

	// Battery on the right. M5Unified answers -1 on hardware it cannot read,
	// which is a dash rather than a zero.
	const int level = M5.Power.getBatteryLevel();
	const bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
	char battery[10];
	if (level < 0) {
		snprintf(battery, sizeof(battery), "--");
	} else {
		snprintf(battery, sizeof(battery), "%s%d%%", charging ? "+" : "",
		         (int)constrain(level, 0, 100));
	}
	const uint16_t batteryColor = charging ? GOOD : (level < 0 ? DIM : (level > 40 ? FG : (level > 15 ? WARN : BAD)));
	g.setTextColor(batteryColor, BAR);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString(battery, W - 3, top + 3);
	const int batteryWidth = g.textWidth(battery);

	// Radio state as one dot, left of the battery.
	const int dotX = W - 8 - batteryWidth;
	g.fillCircle(dotX, top + 6, 2, radioColor());

	if (note != nullptr && note[0] != '\0') {
		const int left = 3 + g.textWidth(source == nullptr ? "" : source) + 6;
		const int width = dotX - 5 - left;
		if (width > 20) {
			g.setTextColor(FG, BAR);
			g.setTextDatum(textdatum_t::top_left);
			String cut(note);
			while (cut.length() > 1 && (int)g.textWidth(cut.c_str()) > width) {
				cut.remove(cut.length() - 1);
			}
			g.drawString(cut.c_str(), left, top + 3);
		}
	}
}

}  // namespace ui
