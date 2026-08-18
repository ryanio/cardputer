#pragma once

// NVS on a desktop: a map that writes itself next to the binary, so the
// simulator remembers a network and a menu position between runs the way the
// device remembers them across a reboot.

#include <Arduino.h>

#include <map>

class Preferences {
public:
	bool begin(const char *name, bool readOnly = false);
	void end();

	int32_t getInt(const char *key, int32_t fallback = 0);
	size_t putInt(const char *key, int32_t value);
	float getFloat(const char *key, float fallback = 0.0f);
	size_t putFloat(const char *key, float value);
	bool getBool(const char *key, bool fallback = false);
	size_t putBool(const char *key, bool value);
	String getString(const char *key, const char *fallback = "");
	size_t putString(const char *key, const char *value);
	bool isKey(const char *key);
	bool remove(const char *key);
	bool clear();

private:
	void load();
	void save();

	std::string _name;
	std::map<std::string, std::string> _values;
	bool _open = false;
};
