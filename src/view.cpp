#include "view.h"

#include <math.h>

#include <new>
#include <vector>

#include "motion.h"
#include "store.h"
#include "ui.h"
#include "version.h"

namespace view {

namespace {

// The menu is a carousel: one card in the middle, its neighbours leaning in
// from both sides, and the strip slides when the selection moves.
//
// A grid of eight fits on 240x135 and reads fine, which is what this was. What
// it could not do is say that there is more: the tenth view sat on a second
// page nobody had a reason to look for. One card at a time shows less at a
// glance and is far clearer about which way the rest of them live, and it is
// the shape a wrist can turn, which is the other half of why it changed.
constexpr int STRIP_H = 84;     // the part that moves, and the sprite over it
constexpr int CARD_BIG = 80;    // the one in the middle
constexpr int CARD_SMALL = 52;  // the ones leaning in
constexpr int CARD_MID_Y = 44;  // every card is centred on this row
constexpr int SPACING = 70;     // between the middles of two cards
constexpr int SOURCE_Y = 92;    // under the strip: which source it reads
constexpr int FOOT_Y = 114;     // and the row of hints and dots
constexpr uint32_t STATUS_MS = 2000;
constexpr const char *MENU_SOURCE = "flint " FW_VERSION;
constexpr const char *SELECTED_KEY = "sys.view";

// How much of the remaining distance the strip closes each frame. A constant
// fraction is an ease out for free: fast off the mark, slow into the detent.
constexpr float EASE = 0.42f;
constexpr float SETTLED = 0.02f;
constexpr uint32_t FRAME_MS = 8;
constexpr int SKIM = 3;  // up and down take three cards at a time

// Tilt spins it. Past the threshold it steps one card and waits, and the wait
// shortens the further the unit goes over, so a lean walks and a flick runs.
// The threshold is deliberately past where a unit sits in a hand: a menu that
// creeps while somebody is reading it would be a fault, not a feature.
constexpr float SPIN_ON = 0.45f;
constexpr uint32_t SPIN_SLOW_MS = 600;
constexpr uint32_t SPIN_FAST_MS = 110;

// Registration runs before setup, so the list lives inside the function and is
// built on first use rather than at static init time.
std::vector<const View *> &registry()
{
	static std::vector<const View *> views;
	return views;
}

int activeIndex = -1;  // -1 is the menu
int selected = 0;
bool dirty = true;

// Where the strip actually is, in cards, while it catches up with selected.
float position = 0.0f;
uint32_t framedAt = 0;
uint32_t spunAt = 0;
int textFor = -1;  // which card the rows under the strip are describing

// The strip is drawn here and pushed in one write. Cards drawn straight onto
// the panel tear and flicker, because the panel shows a frame while it is
// still being built, and a slide is exactly when that shows: the unit reports
// 23ms to draw one that way. 240x84 at 16 bits is 40KB, taken when the menu
// opens and given back when a view does, which a unit with 271KB of largest
// block and 183KB of low water under TLS can spare. Without it the menu still
// works: it steps to the next card rather than sliding.
M5Canvas *strip = nullptr;

uint32_t statusDrawn = 0;
char noteText[32] = {0};
uint32_t noteUntil = 0;

const char *sourceName()
{
	const View *v = active();
	return v == nullptr ? MENU_SOURCE : v->source;
}

void drawStatus()
{
	const bool showNote = noteText[0] != '\0' && (int32_t)(millis() - noteUntil) < 0;
	ui::statusBar(sourceName(), showNote ? noteText : nullptr);
	statusDrawn = millis();
}

// Two colors, some of the way between. Everything on a card that is neither
// in the middle nor out at the edge is drawn with this, so a card arriving
// takes on its colour as it comes rather than at the moment it lands.
uint16_t mix(uint16_t a, uint16_t b, float t)
{
	if (t <= 0.0f) {
		return a;
	}
	if (t >= 1.0f) {
		return b;
	}
	const int ar = (a >> 11) & 0x1F;
	const int ag = (a >> 5) & 0x3F;
	const int ab = a & 0x1F;
	const int br = (b >> 11) & 0x1F;
	const int bg = (b >> 5) & 0x3F;
	const int bb = b & 0x1F;
	const int r = ar + (int)lroundf((float)(br - ar) * t);
	const int g = ag + (int)lroundf((float)(bg - ag) * t);
	const int bl = ab + (int)lroundf((float)(bb - ab) * t);
	return (uint16_t)((r << 11) | (g << 5) | bl);
}

// One card, `away` cards from the middle. Everything about it comes off that
// number: how big it is, where it sits, and how far its colours have travelled
// between the card in the middle and the ones leaning in.
void drawCard(lgfx::LovyanGFX &g, int i, float away)
{
	const View *v = at(i);
	if (v == nullptr) {
		return;
	}
	const float d = fabsf(away);
	const float t = d > 1.0f ? 1.0f : d;

	const int size = CARD_BIG - (int)lroundf((float)(CARD_BIG - CARD_SMALL) * t);
	const int cx = ui::W / 2 + (int)lroundf(away * (float)SPACING);
	const int x = cx - size / 2;
	const int y = CARD_MID_Y - size / 2;
	if (x + size < 0 || x > ui::W) {
		return;
	}

	const uint16_t fill = mix(ui::CORAL, ui::PANEL, t);
	g.fillRoundRect(x, y, size, size, 5, fill);
	if (t > 0.4f) {
		g.drawRoundRect(x, y, size, size, 5, ui::RULE);
	}

	const uint16_t ink = mix(ui::BG, ui::CORAL, t);
	if (v->icon < icons::COUNT) {
		const icons::Icon &art = icons::ALL[v->icon];
		// High in the card while it is big enough to carry a name under the
		// icon, centred once it is not.
		const int top = t < 0.5f ? y + 12 : y + (size - art.height) / 2;
		g.drawBitmap(cx - art.width / 2, top, art.data, art.width, art.height, ink);
	}

	// The badge is the key that opens it: 1 to 9, then 0 for the tenth.
	if (i < 10) {
		char badge[3];
		snprintf(badge, sizeof(badge), "%d", (i + 1) % 10);
		g.setFont(&fonts::Font0);
		g.setTextColor(mix(ui::BG, ui::RULE, t), fill);
		g.setTextDatum(textdatum_t::top_left);
		g.drawString(badge, x + 5, y + 4);
	}

	// The name only fits on the card in the middle, so it arrives with it.
	if (t < 0.5f) {
		g.setFont(&fonts::Font2);
		g.setTextColor(mix(ui::BG, fill, t * 2.0f), fill);
		g.setTextDatum(textdatum_t::top_center);
		g.drawString(v->name, cx, y + 50);
	}
}

// Neighbours first, so the card in the middle keeps its edges.
void paintStrip(lgfx::LovyanGFX &g)
{
	const int total = count();
	const int first = (int)floorf(position) - 1;
	g.fillRect(0, 0, ui::W, STRIP_H, ui::BG);
	for (int pass = 0; pass < 2; pass++) {
		for (int i = first; i <= first + 3; i++) {
			if (i < 0 || i >= total) {
				continue;
			}
			const float away = (float)i - position;
			const bool near = fabsf(away) < 0.5f;
			if (near == (pass == 1)) {
				drawCard(g, i, away);
			}
		}
	}
}

// Under the strip: what the card in the middle reads, and where it sits in the
// list. Redrawn only when that card changes, so a slide does not rewrite it
// fifteen times on the way past.
void drawRows(int i)
{
	const View *v = at(i);
	if (v == nullptr) {
		return;
	}
	const int total = count();
	M5GFX &g = ui::gfx();

	g.startWrite();
	g.fillRect(0, STRIP_H, ui::W, ui::BODY_H - STRIP_H, ui::BG);
	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_center);
	g.drawString(v->source, ui::W / 2, SOURCE_Y);

	g.setTextDatum(textdatum_t::top_left);
	g.drawString("enter opens", 4, FOOT_Y);
	g.setTextDatum(textdatum_t::top_right);
	g.drawString("esc backs out", ui::W - 4, FOOT_Y);

	// One dot a view, which is the only thing on screen that says how many
	// there are. Past twenty they would run into the hints, and a count of
	// that many needs a different menu rather than smaller dots.
	if (total <= 20) {
		const int pitch = 6;
		const int left = ui::W / 2 - (total - 1) * pitch / 2;
		for (int n = 0; n < total; n++) {
			const int x = left + n * pitch;
			if (n == i) {
				g.fillRect(x - 1, FOOT_Y + 2, 3, 3, ui::CORAL);
			} else {
				g.fillRect(x, FOOT_Y + 3, 1, 1, ui::RULE);
			}
		}
	}
	g.endWrite();
}

void openStrip()
{
	if (strip != nullptr) {
		return;
	}
	strip = new (std::nothrow) M5Canvas(&ui::gfx());
	if (strip == nullptr) {
		return;
	}
	strip->setColorDepth(16);
	if (strip->createSprite(ui::W, STRIP_H) == nullptr) {
		delete strip;
		strip = nullptr;
		// Once. The menu asks again on every move, and a line a frame would
		// bury whatever the monitor was being read for.
		static bool said = false;
		if (!said) {
			said = true;
			Serial.println("menu: no sprite, the strip steps instead of sliding");
		}
	}
}

void closeStrip()
{
	if (strip == nullptr) {
		return;
	}
	strip->deleteSprite();
	delete strip;
	strip = nullptr;
}

// One frame of the slide. `force` is a repaint asking for a frame whether or
// not anything moved.
void menuFrame(bool force)
{
	const uint32_t now = millis();
	if (!force && now - framedAt < FRAME_MS) {
		return;
	}
	// Without the sprite there is nothing to animate into, and a slide drawn
	// straight onto the panel is a flicker per frame. Better to arrive.
	if (strip == nullptr) {
		position = (float)selected;
	}
	const float target = (float)selected;
	const bool moving = fabsf(target - position) > SETTLED;
	if (!moving && !force) {
		return;
	}
	framedAt = now;

	position += (target - position) * EASE;
	if (fabsf(target - position) <= SETTLED) {
		position = target;
	}

	if (strip != nullptr) {
		paintStrip(*strip);
		strip->pushSprite(0, 0);
	} else {
		paintStrip(ui::gfx());
	}

	const int nearest = (int)lroundf(position);
	if (nearest != textFor) {
		textFor = nearest;
		drawRows(nearest);
	}
}

// The wrist, on the menu. Nothing here opens anything: a lean moves the
// selection and a shake throws it somewhere else, and enter is still the only
// way in.
void menuMotion()
{
	if (!motion::available() || count() == 0) {
		return;
	}
	if (motion::shaken()) {
		selected = (int)(millis() % (uint32_t)count());
		return;
	}

	const float lean = motion::steerX();
	const float mag = fabsf(lean);
	if (mag < SPIN_ON) {
		spunAt = 0;
		return;
	}
	const float over = (mag - SPIN_ON) / (1.0f - SPIN_ON);
	const uint32_t wait = SPIN_SLOW_MS - (uint32_t)((float)(SPIN_SLOW_MS - SPIN_FAST_MS) * over);
	const uint32_t now = millis();
	if (spunAt != 0 && now - spunAt < wait) {
		return;
	}
	spunAt = now;
	selected = constrain(selected + (lean > 0.0f ? 1 : -1), 0, count() - 1);
}

void drawMenu()
{
	ui::clearBody();
	if (count() == 0) {
		ui::message("no views", "nothing registered");
		return;
	}
	openStrip();
	textFor = -1;
	menuFrame(true);
}

void drawActive()
{
	const View *v = active();
	if (v == nullptr) {
		drawMenu();
		return;
	}
	if (v->draw != nullptr) {
		v->draw();
	} else {
		ui::message(v->name, "nothing to draw yet");
	}
}

bool readKey(Key &out)
{
	if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
		return false;
	}
	Keyboard_Class::KeysState &st = M5Cardputer.Keyboard.keysState();

	out.enter = st.enter;
	out.del = st.del;
	out.space = st.space;
	out.tab = st.tab;
	out.fn = st.fn;
	out.shift = st.shift;
	out.ctrl = st.ctrl;
	out.opt = st.opt;
	out.alt = st.alt;
	out.ch = st.word.empty() ? 0 : st.word.front();

	switch (out.ch) {
		case ';':
			out.up = true;
			break;
		case '.':
			out.down = true;
			break;
		case ',':
			out.left = true;
			break;
		case '/':
			out.right = true;
			break;
		default:
			break;
	}
	return true;
}

void menuKey(const Key &k)
{
	const int total = count();
	if (total == 0) {
		return;
	}

	// The ends are ends rather than a wrap. A strip that jumps the whole way
	// back when you step off the last card reads as a glitch, and the wrap was
	// only ever a way to reach the far end of a grid quickly, which up and
	// down now do three at a time.
	int next = selected;
	if (k.left) {
		next--;
	} else if (k.right) {
		next++;
	} else if (k.up) {
		next -= SKIM;
	} else if (k.down) {
		next += SKIM;
	} else if (k.enter || k.space) {
		open(selected);
		return;
	} else if (k.ch >= '0' && k.ch <= '9') {
		// The badge drawn on the card, which is why it stops at ten.
		const int i = k.ch == '0' ? 9 : k.ch - '1';
		if (i < total) {
			selected = i;
			open(i);
		}
		return;
	} else {
		return;
	}

	// Nothing is drawn here. The frame that follows carries the strip over,
	// which is what makes a keypress and a lean look like the same movement.
	selected = constrain(next, 0, total - 1);
}

}  // namespace

void add(const View *v)
{
	if (v == nullptr || v->name == nullptr) {
		return;
	}
	std::vector<const View *> &views = registry();
	auto slot = views.begin();
	while (slot != views.end() && (*slot)->order <= v->order) {
		++slot;
	}
	views.insert(slot, v);
}

int count()
{
	return (int)registry().size();
}

const View *at(int index)
{
	if (index < 0 || index >= count()) {
		return nullptr;
	}
	return registry()[index];
}

void begin()
{
	const int last = count() - 1;
	const int saved = store::getInt(SELECTED_KEY, 0);
	selected = last < 0 ? 0 : constrain(saved, 0, last);
	// The strip starts where it was left rather than sliding there on boot.
	position = (float)selected;
	activeIndex = -1;
	dirty = true;
	Serial.printf("view: %d views registered\n", count());
}

void loop()
{
	Key k;
	if (readKey(k)) {
		// The exit convention. Taken before any view sees it, so no screen can
		// hold anyone. Fn and backtick together type the character instead,
		// which a passphrase is allowed to contain.
		if (k.ch == '`' && !k.fn) {
			back();
		} else if (inMenu()) {
			menuKey(k);
		} else {
			const View *v = active();
			if (v != nullptr && v->key != nullptr) {
				v->key(k);
			}
		}
	}

	// On the stock firmware the G0 button on the top edge is home, so it is
	// home here too. It is also the way out if a key ever sticks.
	if (M5Cardputer.BtnA.wasPressed()) {
		back();
	}

	const View *v = active();
	if (v != nullptr && v->tick != nullptr) {
		v->tick();
	} else if (v == nullptr) {
		menuMotion();
	}

	const bool wantsBar = v == nullptr || !v->fullScreen;
	if (dirty) {
		drawActive();
		dirty = false;
		if (wantsBar) {
			drawStatus();
		}
	} else {
		// The menu is the one screen that draws between repaints, because the
		// strip is somewhere between two cards and has to keep going.
		if (v == nullptr) {
			menuFrame(false);
		}
		if (wantsBar && millis() - statusDrawn > STATUS_MS) {
			drawStatus();
		}
	}
}

void open(int index)
{
	const View *next = at(index);
	if (next == nullptr) {
		return;
	}
	const View *current = active();
	if (current != nullptr && current->leave != nullptr) {
		current->leave();
	}

	activeIndex = index;
	selected = index;
	position = (float)index;
	// 42KB back to whoever needs it, which on this device is a TLS session.
	closeStrip();
	store::setInt(SELECTED_KEY, index);
	if (next->enter != nullptr) {
		next->enter();
	}
	repaint();
}

void back()
{
	if (inMenu()) {
		return;
	}
	const View *current = active();
	if (current != nullptr && current->leave != nullptr) {
		current->leave();
	}
	activeIndex = -1;
	repaint();
}

bool inMenu()
{
	return activeIndex < 0;
}

const View *active()
{
	return inMenu() ? nullptr : at(activeIndex);
}

void repaint()
{
	dirty = true;
}

void note(const char *text, uint32_t ms)
{
	if (text == nullptr) {
		noteText[0] = '\0';
	} else {
		snprintf(noteText, sizeof(noteText), "%s", text);
	}
	noteUntil = millis() + ms;
	statusDrawn = 0;
}

}  // namespace view
