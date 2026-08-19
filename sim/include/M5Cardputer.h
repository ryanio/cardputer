#pragma once

// The device object, backed by SDL instead of hardware. M5GFX is the real
// library here, running its own desktop panel, so the pixels and the fonts are
// the ones the panel draws. What is simulated is everything around the screen:
// the keyboard, the battery and the G0 button.

#include <Arduino.h>
#include <M5GFX.h>

#include <vector>

#define KEY_BACKSPACE 0x2a
#define KEY_TAB 0x2b
#define KEY_ENTER 0x28
#define KEY_FN 0xff
#define KEY_OPT 0x00

class Keyboard_Class {
public:
	struct KeysState {
		bool tab = false;
		bool fn = false;
		bool shift = false;
		bool ctrl = false;
		bool opt = false;
		bool alt = false;
		bool del = false;
		bool enter = false;
		bool space = false;
		uint8_t modifiers = 0;
		std::vector<char> word;

		void reset()
		{
			tab = fn = shift = ctrl = opt = alt = del = enter = space = false;
			modifiers = 0;
			word.clear();
		}
	};

	void begin();
	void update();  // polls SDL and works out what changed

	uint8_t isPressed()
	{
		return _pressed;
	}
	bool isChange()
	{
		return _changed;
	}
	KeysState &keysState()
	{
		return _state;
	}

	// Scripted input, for the guided tour and for tests. A queued key is only
	// delivered on a frame where nothing real was pressed.
	void queue(char c);

private:
	KeysState _state;
	uint8_t _pressed = 0;
	bool _changed = false;
	std::vector<char> _queued;
};

namespace m5 {

// Real sound, mixed in sim/src/speaker_sim.cpp: SDL on the desktop, Web Audio
// in the browser. Beat and Calm are half a view without it.
class Speaker_Class {
public:
	bool begin();
	void end();
	void setVolume(uint8_t volume);
	void setChannelVolume(uint8_t channel, uint8_t volume);
	bool tone(float frequency, uint32_t duration = 0, int channel = -1, bool stopCurrent = true);
	bool playRaw(const int8_t *data, size_t length, uint32_t rate = 44100, bool stereo = false,
	             uint32_t repeat = 1, int channel = -1, bool stopCurrent = false);
	void stop();
	void stop(uint8_t channel);
	bool isPlaying() const;
	bool isEnabled() const;
};

// The BMI270, simulated. The mouse is the wrist: how far the pointer sits from
// the middle of the window is how far the unit is tipped, holding the left
// button rattles it, and F2 turns it face down. Enough to drive every gesture
// src/motion.cpp reads, which is what keeps a motion view something anybody
// can look at without a unit.
class IMU_Class {
public:
	bool isEnabled() const
	{
		return true;
	}
	bool getAccel(float *x, float *y, float *z);

	// A hand nobody has to hold. --tilt pins the lean where a screenshot wants
	// it, and --shake rattles the unit on a timer, which is how a motion view
	// gets captured by a script rather than by somebody waving a mouse.
	void setTilt(float x, float y);
	void setShaking(bool shaking);

	// A wrist that keeps turning: the lean rolls all the way round once every
	// so many seconds. It is how a falling, rolling view gets a strip of
	// frames worth looking at without anybody holding the unit.
	void setOrbit(float seconds);
};

class Power_Class {
public:
	enum is_charging_t { is_discharging = 0, is_charging, charge_unknown };

	int32_t getBatteryLevel()
	{
		return _level;
	}
	is_charging_t isCharging()
	{
		return _charging ? is_charging : is_discharging;
	}

	// The simulator can pretend, which is the only way to see what the status
	// bar does at 12 percent without waiting for a battery to get there.
	void setBatteryLevel(int32_t level)
	{
		_level = level;
	}
	void setCharging(bool charging)
	{
		_charging = charging;
	}

private:
	int32_t _level = 87;
	bool _charging = false;
};

class Button_Class {
public:
	bool wasPressed();
	void press();

private:
	bool _pending = false;
};

class M5Unified {
public:
	struct config_t {
		uint32_t serial_baudrate = 115200;
	};

	config_t config()
	{
		return config_t();
	}
	void begin();
	void begin(config_t cfg);
	void update();

	M5GFX Display;
	M5GFX &Lcd = Display;
	Power_Class Power;
	Speaker_Class Speaker;
	IMU_Class Imu;

	Button_Class &getButton(size_t)
	{
		return _button;
	}

private:
	Button_Class _button;
};

class M5_CARDPUTER {
public:
	void begin(bool enableKeyboard = true);
	void begin(M5Unified::config_t cfg, bool enableKeyboard = true);

	void update();

	M5GFX &Display;
	M5GFX &Lcd;
	Power_Class &Power;
	Speaker_Class &Speaker;
	Button_Class &BtnA;
	Keyboard_Class Keyboard;

	M5_CARDPUTER();
};

}  // namespace m5

extern m5::M5Unified M5;
extern m5::M5_CARDPUTER M5Cardputer;
