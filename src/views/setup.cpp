#include "../net.h"
#include "../ui.h"
#include "../version.h"
#include "../view.h"

// WiFi typed on the device, so a unit works for whoever holds it. Nothing here
// reaches the compiled fallback in include/secrets.h: what gets typed lands in
// NVS and wins from then on.
namespace {

enum class Screen : uint8_t { Status, Picking, Typing, Joining };

constexpr int SCAN_RUNNING = -1;
constexpr int SCAN_FAILED = -2;
constexpr int MAX_LISTED = 24;
constexpr int ROWS = 5;  // list rows on screen at once
constexpr size_t SSID_MAX = 32;
constexpr size_t PASSPHRASE_MAX = 63;
constexpr uint32_t SPIN_MS = 120;

constexpr const char *ACTIONS[] = {
    "scan for networks",
    "type a network name",
    "forget this network",
};
constexpr int ACTION_COUNT = sizeof(ACTIONS) / sizeof(ACTIONS[0]);

Screen screen = Screen::Status;
int action = 0;
int pick = 0;
int pickTop = 0;
int listed = 0;
int order[MAX_LISTED];
bool scanning = false;
bool typingSsid = false;
String targetSsid;
String entry;
uint32_t lastSpin = 0;
net::Wifi seenState = net::Wifi::Off;

const char *stateText()
{
	switch (net::state()) {
		case net::Wifi::Online:
			return "online";
		case net::Wifi::Joining:
			return "joining";
		case net::Wifi::Failed:
			return "join failed";
		default:
			return "idle";
	}
}

uint16_t stateColor()
{
	switch (net::state()) {
		case net::Wifi::Online:
			return ui::GOOD;
		case net::Wifi::Joining:
			return ui::WARN;
		case net::Wifi::Failed:
			return ui::BAD;
		default:
			return ui::DIM;
	}
}

void toStatus()
{
	screen = Screen::Status;
	entry = "";
	view::repaint();
}

void startScan()
{
	listed = 0;
	pick = 0;
	pickTop = 0;
	scanning = net::scanStart();
	screen = Screen::Picking;
	view::repaint();
}

// Strongest first, and a hidden network has no name to show.
void collectScan()
{
	const int n = net::scanCount();
	if (n == SCAN_RUNNING) {
		return;
	}
	scanning = false;
	listed = 0;
	if (n > 0) {
		for (int i = 0; i < n && listed < MAX_LISTED; i++) {
			if (net::scanSsid(i).isEmpty()) {
				continue;
			}
			int slot = listed++;
			while (slot > 0 && net::scanRssi(order[slot - 1]) < net::scanRssi(i)) {
				order[slot] = order[slot - 1];
				slot--;
			}
			order[slot] = i;
		}
	}
	view::repaint();
}

void beginTyping(const String &ssid, bool forSsid)
{
	targetSsid = ssid;
	typingSsid = forSsid;
	entry = "";
	screen = Screen::Typing;
	view::repaint();
}

void joinWith(const String &ssid, const String &pass)
{
	net::scanClear();
	net::saveCredentials(ssid.c_str(), pass.c_str());
	seenState = net::state();
	screen = Screen::Joining;
	view::repaint();
}

void drawStatus()
{
	char text[48];
	ui::clearBody();
	ui::title("Setup");

	const char *name = net::ssid();
	snprintf(text, sizeof(text), "wifi  %s", name[0] == '\0' ? "none set" : name);
	ui::line(0, text);

	if (net::online()) {
		snprintf(text, sizeof(text), "%s  %s  %d dBm", stateText(), net::ip().toString().c_str(),
		         (int)net::rssi());
	} else {
		const char *where = !net::haveCredentials() ? "no network saved yet"
		                    : net::credentialsAreStored() ? "saved on this unit"
		                                                  : "built into this build";
		snprintf(text, sizeof(text), "%s  %s", stateText(), where);
	}
	ui::line(1, text, stateColor());

	constexpr uint8_t ACTION_ICONS[] = {icons::WIFI, icons::KEYBOARD, icons::TRASH};
	for (int i = 0; i < ACTION_COUNT; i++) {
		const bool on = i == action;
		const bool dim = i == 2 && !net::credentialsAreStored();
		const uint16_t color = dim ? ui::RULE : (on ? ui::CORAL : ui::FG);
		ui::line(3 + i, ACTIONS[i], color, 24);
		ui::icon(ACTION_ICONS[i], 5, ui::TITLE_H + (3 + i) * ui::LINE_H + 2, color);
	}
}

void drawPicking()
{
	char text[48];
	ui::clearBody();

	if (scanning) {
		ui::title("Networks");
		ui::line(1, "  looking around", ui::DIM);
		ui::spinner(ui::W / 2, 76);
		return;
	}

	if (listed == 0) {
		ui::title("Networks");
		ui::line(1, "  nothing found", ui::DIM);
		ui::line(3, "  enter scans again", ui::DIM);
		ui::line(4, "  del goes back", ui::DIM);
		return;
	}

	snprintf(text, sizeof(text), "Networks (%d)", listed);
	ui::title(text);

	if (pick < pickTop) {
		pickTop = pick;
	} else if (pick >= pickTop + ROWS) {
		pickTop = pick - ROWS + 1;
	}

	for (int row = 0; row < ROWS; row++) {
		const int i = pickTop + row;
		if (i >= listed) {
			break;
		}
		const bool on = i == pick;
		snprintf(text, sizeof(text), "%s%s%s", on ? "> " : "  ",
		         net::scanOpen(order[i]) ? "" : "* ", net::scanSsid(order[i]).c_str());
		ui::line(row, text, on ? ui::CORAL : ui::FG);
		snprintf(text, sizeof(text), "%d", (int)net::scanRssi(order[i]));
		ui::lineAt(ui::TITLE_H + row * ui::LINE_H, text, ui::DIM, textdatum_t::top_right);
	}
	ui::lineAt(ui::TITLE_H + ROWS * ui::LINE_H, "enter picks   del goes back", ui::DIM);
}

void drawTyping()
{
	char text[64];
	ui::clearBody();

	if (typingSsid) {
		ui::title("Network name");
	} else {
		snprintf(text, sizeof(text), "Passphrase for %s", targetSsid.c_str());
		ui::title(text);
	}

	snprintf(text, sizeof(text), "%s_", entry.c_str());
	ui::line(1, text);

	ui::line(3, "enter saves and joins", ui::DIM);
	ui::line(4, "del erases   esc cancels", ui::DIM);
	if (!typingSsid) {
		ui::line(5, "fn esc for a backtick", ui::DIM);
	}
}

void drawJoining()
{
	char text[48];
	ui::clearBody();
	ui::title("Joining");
	snprintf(text, sizeof(text), "%s", net::ssid());
	ui::line(0, text);
	ui::line(1, stateText(), stateColor());

	if (net::state() == net::Wifi::Joining) {
		ui::spinner(ui::W / 2, 84);
		return;
	}
	if (net::online()) {
		snprintf(text, sizeof(text), "%s", net::ip().toString().c_str());
		ui::line(2, text, ui::GOOD);
		ui::line(4, "saved on this unit", ui::DIM);
	} else {
		ui::line(2, "check the passphrase", ui::DIM);
		ui::line(4, "del goes back", ui::DIM);
	}
}

void draw()
{
	switch (screen) {
		case Screen::Picking:
			drawPicking();
			break;
		case Screen::Typing:
			drawTyping();
			break;
		case Screen::Joining:
			drawJoining();
			break;
		default:
			drawStatus();
			break;
	}
}

void enter()
{
	screen = Screen::Status;
	action = 0;
	entry = "";
}

void leave()
{
	net::scanClear();
	scanning = false;
	entry = "";
}

void tick()
{
	if (screen == Screen::Picking && scanning) {
		collectScan();
		if (millis() - lastSpin > SPIN_MS) {
			lastSpin = millis();
			view::repaint();
		}
		return;
	}
	if (screen == Screen::Joining) {
		if (net::state() != seenState) {
			seenState = net::state();
			view::repaint();
		} else if (net::state() == net::Wifi::Joining && millis() - lastSpin > SPIN_MS) {
			lastSpin = millis();
			view::repaint();
		}
	}
}

bool statusKey(const view::Key &k)
{
	if (k.up) {
		action = (action + ACTION_COUNT - 1) % ACTION_COUNT;
	} else if (k.down) {
		action = (action + 1) % ACTION_COUNT;
	} else if (k.enter || k.right) {
		switch (action) {
			case 0:
				startScan();
				return true;
			case 1:
				beginTyping("", true);
				return true;
			default:
				net::forgetCredentials();
				view::note("network forgotten");
				break;
		}
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool pickingKey(const view::Key &k)
{
	if (k.del || k.left) {
		net::scanClear();
		toStatus();
	} else if (scanning) {
		return true;  // a scan is running, so there is nothing to pick yet
	} else if (listed == 0 && k.enter) {
		startScan();
	} else if (k.up && listed > 0) {
		pick = (pick + listed - 1) % listed;
	} else if (k.down && listed > 0) {
		pick = (pick + 1) % listed;
	} else if ((k.enter || k.right) && listed > 0) {
		const String name = net::scanSsid(order[pick]);
		if (net::scanOpen(order[pick])) {
			joinWith(name, "");
		} else {
			beginTyping(name, false);
		}
		return true;
	} else {
		return false;
	}
	view::repaint();
	return true;
}

bool typingKey(const view::Key &k)
{
	const size_t limit = typingSsid ? SSID_MAX : PASSPHRASE_MAX;

	if (k.enter) {
		if (typingSsid) {
			if (entry.isEmpty()) {
				return true;
			}
			beginTyping(entry, false);
		} else {
			joinWith(targetSsid, entry);
		}
		return true;
	}
	if (k.del) {
		if (entry.isEmpty()) {
			toStatus();
		} else {
			entry.remove(entry.length() - 1);
			view::repaint();
		}
		return true;
	}
	if (k.space && entry.length() < limit) {
		entry += ' ';
		view::repaint();
		return true;
	}
	// The arrow keys print their own characters, and a passphrase is as likely
	// to hold one as any other symbol, so nothing here treats them as cursors.
	if (k.ch >= ' ' && k.ch <= '~' && entry.length() < limit) {
		entry += k.ch;
		view::repaint();
		return true;
	}
	return false;
}

bool key(const view::Key &k)
{
	switch (screen) {
		case Screen::Picking:
			return pickingKey(k);
		case Screen::Typing:
			return typingKey(k);
		case Screen::Joining:
			if (k.del || k.enter || k.left) {
				toStatus();
				return true;
			}
			return false;
		default:
			return statusKey(k);
	}
}

const view::View kSetup = {
    .name = "Setup",
    .source = "SETUP",
    .order = view::ORDER_LAST,
    .icon = icons::SETTINGS,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kSetup);
