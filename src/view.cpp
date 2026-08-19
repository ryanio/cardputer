#include "view.h"

#include <vector>

#include "store.h"
#include "ui.h"
#include "version.h"

namespace view {

namespace {

// The menu is a grid of cards rather than a list. On 240x135 a list spends
// most of its width on nothing, and six cards read at arm's length.
constexpr int COLS = 4;
constexpr int GRID_ROWS = 2;
constexpr int MARGIN = 4;
constexpr int GAP = 4;
constexpr int CARD_W = (ui::W - MARGIN * 2 - GAP * (COLS - 1)) / COLS;
constexpr int CARD_H = 50;
constexpr int HINT_Y = 113;
constexpr int VISIBLE = COLS * GRID_ROWS;
constexpr uint32_t STATUS_MS = 2000;
constexpr const char *MENU_SOURCE = "flint " FW_VERSION;
constexpr const char *SELECTED_KEY = "sys.view";

// Registration runs before setup, so the list lives inside the function and is
// built on first use rather than at static init time.
std::vector<const View *> &registry()
{
	static std::vector<const View *> views;
	return views;
}

int activeIndex = -1;  // -1 is the menu
int selected = 0;
int scroll = 0;
bool dirty = true;

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

// Scroll by whole rows, so a seventh view pushes the grid rather than
// reflowing it. Called before drawing rather than during it, so moving the
// selection can tell whether the grid moved under it.
void applyScroll()
{
	const int selectedRow = selected / COLS;
	const int firstRow = scroll / COLS;
	if (selectedRow < firstRow) {
		scroll = selectedRow * COLS;
	} else if (selectedRow >= firstRow + GRID_ROWS) {
		scroll = (selectedRow - GRID_ROWS + 1) * COLS;
	}
}

// One card, in place, over whatever is already there. The card is opaque and
// covers its own rectangle, so moving the selection never has to clear first.
void drawCard(int i)
{
	const View *v = at(i);
	const int slot = i - scroll;
	if (v == nullptr || slot < 0 || slot >= VISIBLE) {
		return;
	}

	M5GFX &g = ui::gfx();
	const int x = MARGIN + (slot % COLS) * (CARD_W + GAP);
	const int y = MARGIN + (slot / COLS) * (CARD_H + GAP);
	const bool on = i == selected;

	g.startWrite();
	ui::card(x, y, CARD_W, CARD_H, on);
	const uint16_t background = on ? ui::CORAL : ui::PANEL;

	char number[4];
	snprintf(number, sizeof(number), "%d", i + 1);
	g.setFont(&fonts::Font0);
	g.setTextColor(on ? ui::BG : ui::RULE, background);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString(number, x + 4, y + 3);

	ui::icon(v->icon, x + (CARD_W - 16) / 2, y + 4, on ? ui::BG : ui::CORAL);

	g.setFont(&fonts::Font2);
	g.setTextColor(on ? ui::BG : ui::FG, background);
	g.setTextDatum(textdatum_t::top_center);
	g.drawString(v->name, x + CARD_W / 2, y + 21);

	g.setFont(&fonts::Font0);
	g.setTextColor(on ? ui::BG : ui::DIM, background);
	g.drawString(v->source, x + CARD_W / 2, y + CARD_H - 11);
	g.endWrite();
}

void drawMenu()
{
	ui::clearBody();
	M5GFX &g = ui::gfx();

	const int total = count();
	if (total == 0) {
		ui::message("no views", "nothing registered");
		return;
	}

	applyScroll();
	for (int slot = 0; slot < VISIBLE; slot++) {
		drawCard(scroll + slot);
	}

	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString("enter opens    esc always backs out", 4, HINT_Y);
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

	const int was = selected;
	const int wasScroll = scroll;

	if (k.left) {
		selected = (selected + total - 1) % total;
	} else if (k.right) {
		selected = (selected + 1) % total;
	} else if (k.up) {
		selected = selected >= COLS ? selected - COLS : total - 1;
	} else if (k.down) {
		selected = selected + COLS < total ? selected + COLS : 0;
	} else if (k.enter || k.space) {
		open(selected);
		return;
	} else if (k.ch >= '1' && k.ch <= '9') {
		const int i = k.ch - '1';
		if (i < total) {
			selected = i;
			open(i);
		}
		return;
	} else {
		return;
	}

	// Moving the selection repaints two cards, not the screen. Clearing the
	// body first is a black frame the panel is slow enough to show, and on
	// hardware that reads as the whole screen flashing on every arrow key.
	applyScroll();
	if (scroll != wasScroll) {
		repaint();
		return;
	}
	drawCard(was);
	drawCard(selected);
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
	}

	const bool wantsBar = v == nullptr || !v->fullScreen;
	if (dirty) {
		drawActive();
		dirty = false;
		if (wantsBar) {
			drawStatus();
		}
	} else if (wantsBar && millis() - statusDrawn > STATUS_MS) {
		drawStatus();
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
