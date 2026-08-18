#include "view.h"

#include <vector>

#include "store.h"
#include "ui.h"
#include "version.h"

namespace view {

namespace {

constexpr int LIST_TOP = 4;
constexpr int ENTRY_H = 21;
constexpr int HINT_Y = 110;
constexpr int VISIBLE = (HINT_Y - LIST_TOP) / ENTRY_H;
constexpr uint32_t STATUS_MS = 2000;
constexpr const char *MENU_SOURCE = "coral " FW_VERSION;
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

void drawMenu()
{
	ui::clearBody();
	M5GFX &g = ui::gfx();

	const int total = count();
	if (total == 0) {
		ui::message("no views", "nothing registered");
		return;
	}

	if (selected < scroll) {
		scroll = selected;
	} else if (selected >= scroll + VISIBLE) {
		scroll = selected - VISIBLE + 1;
	}

	for (int row = 0; row < VISIBLE; row++) {
		const int i = scroll + row;
		if (i >= total) {
			break;
		}
		const View *v = at(i);
		const int y = LIST_TOP + row * ENTRY_H;
		const bool on = i == selected;

		if (on) {
			g.fillRoundRect(2, y, ui::W - 4, ENTRY_H - 2, 4, ui::CORAL);
		}
		const uint16_t background = on ? ui::CORAL : ui::BG;
		const uint16_t label = on ? ui::BG : ui::FG;

		char number[4];
		snprintf(number, sizeof(number), "%d", i + 1);
		g.setFont(&fonts::Font2);
		g.setTextColor(on ? ui::BG : ui::DIM, background);
		g.setTextDatum(textdatum_t::top_left);
		g.drawString(number, 8, y + 2);

		g.setTextColor(label, background);
		g.drawString(v->name, 26, y + 2);

		g.setFont(&fonts::Font0);
		g.setTextColor(on ? ui::BG : ui::DIM, background);
		g.setTextDatum(textdatum_t::top_right);
		g.drawString(v->source, ui::W - 8, y + 7);
	}

	g.setFont(&fonts::Font0);
	g.setTextColor(ui::DIM, ui::BG);
	g.setTextDatum(textdatum_t::top_left);
	g.drawString("enter opens    ` backs out of anything", 4, HINT_Y);
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

	if (k.up) {
		selected = (selected + total - 1) % total;
		repaint();
	} else if (k.down) {
		selected = (selected + 1) % total;
		repaint();
	} else if (k.enter || k.space || k.right) {
		open(selected);
	} else if (k.ch >= '1' && k.ch <= '9') {
		const int i = k.ch - '1';
		if (i < total) {
			selected = i;
			open(i);
		}
	}
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
	selected = constrain(store::getInt(SELECTED_KEY, 0), 0, max(0, count() - 1));
	activeIndex = -1;
	dirty = true;
	Serial.printf("view: %d views registered\n", count());
}

void loop()
{
	Key k;
	if (readKey(k)) {
		// The exit convention. Taken before any view sees it, so no screen can
		// hold anyone.
		if (k.ch == '`') {
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

	if (dirty) {
		drawActive();
		dirty = false;
		drawStatus();
	} else if (millis() - statusDrawn > STATUS_MS) {
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
