#pragma once

// Enough of Arduino to build the firmware's own source on a desktop. Only what
// src/ actually calls is here: a shim that grows past that is a shim nobody
// can trust.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

template <typename T, typename L, typename H>
constexpr T constrain(T value, L low, H high)
{
	return value < (T)low ? (T)low : (value > (T)high ? (T)high : value);
}

using std::max;
using std::min;

class String : public std::string {
public:
	String() = default;
	String(const char *s) : std::string(s == nullptr ? "" : s) {}
	String(const std::string &s) : std::string(s) {}
	explicit String(int v) : std::string(std::to_string(v)) {}

	const char *c_str() const
	{
		return std::string::c_str();
	}
	bool isEmpty() const
	{
		return empty();
	}
	void remove(size_t index)
	{
		if (index < size()) {
			erase(index);
		}
	}
	void remove(size_t index, size_t count)
	{
		if (index < size()) {
			erase(index, count);
		}
	}
};

inline String operator+(const String &a, const char *b)
{
	return String(std::string(a) + (b == nullptr ? "" : b));
}

// Only a declaration is needed: net.h names Stream in a signature and the
// simulator's network never hands one out.
// The name of the guard matters as much as the class: M5GFX only exposes its
// drawJpg(Stream*) overloads when Stream_h is defined, which is how the real
// Arduino core announces this header. Without it the simulator cannot stream a
// JPEG at the panel and the womp view would not compile here.
#define Stream_h

class Stream {
public:
	virtual ~Stream() = default;
	virtual int available()
	{
		return 0;
	}
	virtual int read()
	{
		return -1;
	}
	virtual size_t readBytes(char *, size_t)
	{
		return 0;
	}
	// The decoder reads into a byte buffer, and Arduino's Stream carries both
	// spellings, so this one does too.
	virtual size_t readBytes(uint8_t *into, size_t want)
	{
		return readBytes((char *)into, want);
	}
};

class IPAddress {
public:
	IPAddress() = default;
	IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _a(a), _b(b), _c(c), _d(d) {}
	String toString() const
	{
		char text[16];
		snprintf(text, sizeof(text), "%u.%u.%u.%u", _a, _b, _c, _d);
		return String(text);
	}

private:
	uint8_t _a = 0, _b = 0, _c = 0, _d = 0;
};

class SerialShim {
public:
	void begin(unsigned long) {}
	void printf(const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		vprintf(format, args);
		va_end(args);
		fflush(stdout);
	}
	void println(const char *s = "")
	{
		::printf("%s\n", s);
		fflush(stdout);
	}
	void print(const char *s)
	{
		::printf("%s", s);
		fflush(stdout);
	}
};

extern SerialShim Serial;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);

// clang-format off
#define log_e(format, ...) Serial.printf("[E] " format "\n", ##__VA_ARGS__)
#define log_w(format, ...) Serial.printf("[W] " format "\n", ##__VA_ARGS__)
#define log_i(format, ...) Serial.printf("[I] " format "\n", ##__VA_ARGS__)
#define log_d(format, ...) do { } while (0)
// clang-format on
