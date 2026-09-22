#pragma once

#include <Arduino.h>

#include "store.h"

namespace net_profiles {

constexpr int MAX_PROFILES = 4;

struct Entry {
	String ssid;
	String password;
};

class Profiles {
public:
	void load()
	{
		count_ = 0;
		if (!decode(store::getString(BLOB_KEY, ""))) {
			migrateLegacy();
		}
	}

	int count() const
	{
		return count_;
	}

	const Entry *at(int index) const
	{
		return index >= 0 && index < count_ ? &entries_[index] : nullptr;
	}

	int find(const char *ssid) const
	{
		if (ssid == nullptr) {
			return -1;
		}
		for (int i = 0; i < count_; i++) {
			if (entries_[i].ssid == ssid) {
				return i;
			}
		}
		return -1;
	}

	bool remember(const String &ssid, const String &password)
	{
		if (ssid.isEmpty()) {
			return false;
		}
		Entry next[MAX_PROFILES];
		next[0] = {ssid, password};
		int nextCount = 1;
		for (int i = 0; i < count_ && nextCount < MAX_PROFILES; i++) {
			if (entries_[i].ssid != ssid) {
				next[nextCount++] = entries_[i];
			}
		}
		if (!persist(next, nextCount)) {
			return false;
		}
		copyFrom(next, nextCount);
		return true;
	}

	bool forget(const char *ssid)
	{
		const int forgotten = find(ssid);
		if (forgotten < 0) {
			return false;
		}
		Entry next[MAX_PROFILES];
		int nextCount = 0;
		for (int i = 0; i < count_; i++) {
			if (i != forgotten) {
				next[nextCount++] = entries_[i];
			}
		}
		if (!persist(next, nextCount)) {
			return false;
		}
		copyFrom(next, nextCount);
		return true;
	}

private:
	static constexpr const char *BLOB_KEY = "sys.wifi";
	static constexpr const char *LEGACY_SSID_KEY = "sys.ssid";
	static constexpr const char *LEGACY_PASS_KEY = "sys.pass";
	static constexpr size_t SSID_MAX = 32;
	static constexpr size_t PASSWORD_MAX = 63;

	Entry entries_[MAX_PROFILES];
	int count_ = 0;

	void copyFrom(const Entry *entries, int count)
	{
		count_ = count;
		for (int i = 0; i < count_; i++) {
			entries_[i] = entries[i];
		}
	}

	bool persist(const Entry *entries, int count)
	{
		String blob("1");
		appendHex(blob, (uint8_t)count);
		for (int i = 0; i < count; i++) {
			if (entries[i].ssid.isEmpty() || entries[i].ssid.length() > SSID_MAX ||
			    entries[i].password.length() > PASSWORD_MAX) {
				return false;
			}
			appendHex(blob, (uint8_t)entries[i].ssid.length());
			appendString(blob, entries[i].ssid);
			appendHex(blob, (uint8_t)entries[i].password.length());
			appendString(blob, entries[i].password);
		}
		// NVS replaces one value atomically, so an interrupted write leaves the
		// complete previous profile set instead of mixing old and new fields.
		return store::setString(BLOB_KEY, blob);
	}

	void migrateLegacy()
	{
		const String ssid = store::getString(LEGACY_SSID_KEY, "");
		if (ssid.isEmpty()) {
			return;
		}
		Entry legacy[1] = {{ssid, store::getString(LEGACY_PASS_KEY, "")}};
		copyFrom(legacy, 1);
		if (persist(legacy, 1)) {
			store::remove(LEGACY_SSID_KEY);
			store::remove(LEGACY_PASS_KEY);
		}
	}

	static int nibble(char c)
	{
		if (c >= '0' && c <= '9') {
			return c - '0';
		}
		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}
		return -1;
	}

	static void appendHex(String &into, uint8_t value)
	{
		constexpr char DIGITS[] = "0123456789abcdef";
		into += DIGITS[value >> 4];
		into += DIGITS[value & 0x0f];
	}

	static void appendString(String &into, const String &value)
	{
		for (size_t i = 0; i < value.length(); i++) {
			appendHex(into, (uint8_t)value[i]);
		}
	}

	static bool readHex(const String &from, size_t &at, uint8_t &value)
	{
		if (at + 2 > from.length()) {
			return false;
		}
		const int high = nibble(from[at]);
		const int low = nibble(from[at + 1]);
		at += 2;
		if (high < 0 || low < 0) {
			return false;
		}
		value = (uint8_t)((high << 4) | low);
		return true;
	}

	static bool readString(const String &from, size_t &at, size_t length, String &value)
	{
		value = "";
		for (size_t i = 0; i < length; i++) {
			uint8_t byte = 0;
			if (!readHex(from, at, byte)) {
				return false;
			}
			value += (char)byte;
		}
		return true;
	}

	bool decode(const String &blob)
	{
		if (blob.isEmpty() || blob[0] != '1') {
			return false;
		}
		size_t at = 1;
		uint8_t count = 0;
		if (!readHex(blob, at, count) || count > MAX_PROFILES) {
			return false;
		}
		Entry decoded[MAX_PROFILES];
		for (uint8_t i = 0; i < count; i++) {
			uint8_t ssidLength = 0;
			uint8_t passwordLength = 0;
			if (!readHex(blob, at, ssidLength) || ssidLength == 0 || ssidLength > SSID_MAX ||
			    !readString(blob, at, ssidLength, decoded[i].ssid) ||
			    !readHex(blob, at, passwordLength) || passwordLength > PASSWORD_MAX ||
			    !readString(blob, at, passwordLength, decoded[i].password)) {
				return false;
			}
		}
		if (at != blob.length()) {
			return false;
		}
		copyFrom(decoded, count);
		return true;
	}
};

}  // namespace net_profiles
