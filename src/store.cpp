#include "store.h"

#include <Preferences.h>

namespace store {

namespace {

constexpr const char *NAMESPACE = "coral";

Preferences prefs;
bool opened = false;

// A key longer than the NVS limit is silently truncated by the driver, which
// makes two settings quietly share one slot. Refuse it instead.
bool valid(const char *key)
{
	if (key == nullptr || key[0] == '\0') {
		return false;
	}
	if (strlen(key) > KEY_MAX) {
		log_e("store: key '%s' is longer than %u characters", key, (unsigned)KEY_MAX);
		return false;
	}
	return opened;
}

}  // namespace

bool begin()
{
	if (opened) {
		return true;
	}
	opened = prefs.begin(NAMESPACE, false);
	if (!opened) {
		log_e("store: could not open the '%s' namespace", NAMESPACE);
	}
	return opened;
}

bool ready()
{
	return opened;
}

int32_t getInt(const char *key, int32_t fallback)
{
	return valid(key) ? prefs.getInt(key, fallback) : fallback;
}

bool setInt(const char *key, int32_t value)
{
	return valid(key) && prefs.putInt(key, value) > 0;
}

float getFloat(const char *key, float fallback)
{
	return valid(key) ? prefs.getFloat(key, fallback) : fallback;
}

bool setFloat(const char *key, float value)
{
	return valid(key) && prefs.putFloat(key, value) > 0;
}

bool getBool(const char *key, bool fallback)
{
	return valid(key) ? prefs.getBool(key, fallback) : fallback;
}

bool setBool(const char *key, bool value)
{
	return valid(key) && prefs.putBool(key, value) > 0;
}

String getString(const char *key, const char *fallback)
{
	return valid(key) ? prefs.getString(key, fallback) : String(fallback);
}

bool setString(const char *key, const String &value)
{
	return valid(key) && prefs.putString(key, value.c_str()) > 0;
}

bool has(const char *key)
{
	return valid(key) && prefs.isKey(key);
}

bool remove(const char *key)
{
	return valid(key) && prefs.remove(key);
}

bool clear()
{
	return opened && prefs.clear();
}

}  // namespace store
