#include <SDL2/SDL.h>

#include <M5Cardputer.h>
#include <Preferences.h>

#include <chrono>
#include <fstream>
#include <sstream>

SerialShim Serial;
m5::M5Unified M5;
m5::M5_CARDPUTER M5Cardputer;

namespace {

const auto started = std::chrono::steady_clock::now();

uint8_t previous[SDL_NUM_SCANCODES] = {0};
bool buttonPending = false;

// US layout, the row a Cardputer prints on its keycaps.
// clang-format off
char shifted(char c)
{
	switch (c) {
		case '1': return '!';
		case '2': return '@';
		case '3': return '#';
		case '4': return '$';
		case '5': return '%';
		case '6': return '^';
		case '7': return '&';
		case '8': return '*';
		case '9': return '(';
		case '0': return ')';
		case '-': return '_';
		case '=': return '+';
		case '[': return '{';
		case ']': return '}';
		case '\\': return '|';
		case ';': return ':';
		case '\'': return '"';
		case ',': return '<';
		case '.': return '>';
		case '/': return '?';
		case '`': return '~';
		default: return c;
	}
}

// The four keys with arrows printed on them report as their characters on the
// device, so the host arrow keys do the same here rather than inventing a
// concept the firmware does not have. Escape is the backtick for the same
// reason: on the device that key is the way out.
char character(SDL_Scancode code, bool shift)
{
	switch (code) {
		case SDL_SCANCODE_UP: return ';';
		case SDL_SCANCODE_DOWN: return '.';
		case SDL_SCANCODE_LEFT: return ',';
		case SDL_SCANCODE_RIGHT: return '/';
		case SDL_SCANCODE_ESCAPE: return '`';
		default: break;
	}
	// clang-format on
	const SDL_Keycode key = SDL_GetKeyFromScancode(code);
	if (key >= 'a' && key <= 'z') {
		return shift ? (char)(key - 32) : (char)key;
	}
	if (key >= 32 && key < 127) {
		return shift ? shifted((char)key) : (char)key;
	}
	return 0;
}

}  // namespace

uint32_t millis()
{
	return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::steady_clock::now() - started)
	    .count();
}

uint32_t micros()
{
	return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
	           std::chrono::steady_clock::now() - started)
	    .count();
}

void delay(uint32_t ms)
{
#if defined(__EMSCRIPTEN__)
	// The browser paces the frames. Sleeping here would just stall one.
	(void)ms;
#else
	SDL_Delay(ms);
#endif
}

void Keyboard_Class::begin()
{
	memset(previous, 0, sizeof(previous));
}

void Keyboard_Class::update()
{
	int count = 0;
	const uint8_t *now = SDL_GetKeyboardState(&count);
	if (now == nullptr) {
		return;
	}
	if (count > SDL_NUM_SCANCODES) {
		count = SDL_NUM_SCANCODES;
	}

	const SDL_Keymod mods = SDL_GetModState();
	const bool shift = (mods & KMOD_SHIFT) != 0;

	_state.reset();
	_changed = false;
	_pressed = 0;

	for (int code = 0; code < count; code++) {
		if (now[code]) {
			_pressed++;
		}
		const bool pressedNow = now[code] && !previous[code];
		previous[code] = now[code];
		if (!pressedNow) {
			continue;
		}
		_changed = true;

		switch (code) {
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
				_state.enter = true;
				continue;
			case SDL_SCANCODE_BACKSPACE:
				_state.del = true;
				continue;
			case SDL_SCANCODE_TAB:
				_state.tab = true;
				continue;
			case SDL_SCANCODE_SPACE:
				_state.space = true;
				_state.word.push_back(' ');
				continue;
			case SDL_SCANCODE_HOME:
			case SDL_SCANCODE_F1:
				// The G0 button on the top edge.
				buttonPending = true;
				continue;
			default:
				break;
		}
		const char c = character((SDL_Scancode)code, shift);
		if (c != 0) {
			_state.word.push_back(c);
		}
	}

	if (!_changed && !_queued.empty()) {
		const char c = _queued.front();
		_queued.erase(_queued.begin());
		_changed = true;
		_pressed = 1;
		if (c == '\n') {
			_state.enter = true;
		} else if (c == '\b') {
			_state.del = true;
		} else if (c == '\t') {
			_state.tab = true;
		} else {
			_state.word.push_back(c);
			_state.space = c == ' ';
		}
		return;
	}

	_state.shift = shift;
	_state.fn = (mods & KMOD_ALT) != 0;
	_state.ctrl = (mods & KMOD_CTRL) != 0;
	_state.opt = (mods & KMOD_GUI) != 0;
}

namespace m5 {

bool Button_Class::wasPressed()
{
	if (!buttonPending) {
		return false;
	}
	buttonPending = false;
	return true;
}

void Button_Class::press()
{
	buttonPending = true;
}

void M5Unified::begin()
{
	Display.begin();
}

void M5Unified::begin(config_t)
{
	Display.begin();
}

void M5Unified::update() {}

M5_CARDPUTER::M5_CARDPUTER()
    : Display(M5.Display),
      Lcd(M5.Display),
      Power(M5.Power),
      Speaker(M5.Speaker),
      BtnA(M5.getButton(0))
{}

void M5_CARDPUTER::begin(bool enableKeyboard)
{
	begin(M5Unified::config_t(), enableKeyboard);
}

void M5_CARDPUTER::begin(M5Unified::config_t cfg, bool enableKeyboard)
{
	M5.begin(cfg);
	if (enableKeyboard) {
		Keyboard.begin();
	}
}

void M5_CARDPUTER::update()
{
	M5.update();
	Keyboard.update();
}

}  // namespace m5

// Settings land in a file beside the binary, so the simulator keeps a network
// and a menu position across runs the way the device keeps them across a boot.
namespace {
std::string storePath(const std::string &name)
{
	return "sim-nvs-" + name + ".txt";
}
}  // namespace

bool Preferences::begin(const char *name, bool)
{
	_name = name == nullptr ? "default" : name;
	_open = true;
	load();
	return true;
}

void Preferences::end()
{
	_open = false;
}

void Preferences::load()
{
	_values.clear();
	std::ifstream in(storePath(_name));
	std::string line;
	while (std::getline(in, line)) {
		const auto split = line.find('=');
		if (split != std::string::npos) {
			_values[line.substr(0, split)] = line.substr(split + 1);
		}
	}
}

void Preferences::save()
{
	std::ofstream out(storePath(_name), std::ios::trunc);
	for (const auto &entry : _values) {
		out << entry.first << "=" << entry.second << "\n";
	}
}

int32_t Preferences::getInt(const char *key, int32_t fallback)
{
	const auto found = _values.find(key);
	return found == _values.end() ? fallback : (int32_t)strtol(found->second.c_str(), nullptr, 10);
}

size_t Preferences::putInt(const char *key, int32_t value)
{
	_values[key] = std::to_string(value);
	save();
	return sizeof(int32_t);
}

float Preferences::getFloat(const char *key, float fallback)
{
	const auto found = _values.find(key);
	return found == _values.end() ? fallback : strtof(found->second.c_str(), nullptr);
}

size_t Preferences::putFloat(const char *key, float value)
{
	_values[key] = std::to_string(value);
	save();
	return sizeof(float);
}

bool Preferences::getBool(const char *key, bool fallback)
{
	const auto found = _values.find(key);
	return found == _values.end() ? fallback : found->second == "1";
}

size_t Preferences::putBool(const char *key, bool value)
{
	_values[key] = value ? "1" : "0";
	save();
	return sizeof(uint8_t);
}

String Preferences::getString(const char *key, const char *fallback)
{
	const auto found = _values.find(key);
	return found == _values.end() ? String(fallback) : String(found->second);
}

size_t Preferences::putString(const char *key, const char *value)
{
	_values[key] = value == nullptr ? "" : value;
	save();
	return strlen(value == nullptr ? "" : value);
}

bool Preferences::isKey(const char *key)
{
	return _values.count(key) > 0;
}

bool Preferences::remove(const char *key)
{
	const bool had = _values.erase(key) > 0;
	save();
	return had;
}

bool Preferences::clear()
{
	_values.clear();
	save();
	return true;
}

void Keyboard_Class::queue(char c)
{
	_queued.push_back(c);
}
