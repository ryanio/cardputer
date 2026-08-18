#pragma once

#include <Arduino.h>

// Settings that survive a reboot, kept in NVS.
//
// The spine deliberately exposes typed key value calls rather than one named
// accessor per setting: views land in parallel and each one owns its own keys,
// so nobody has to edit this header to add a value.
//
// NVS keys are at most 15 characters. Take a prefix so two views cannot
// collide:
//
//   sys.*    the spine        sys.view, sys.tz
//   gas.*    the gas view     gas.alarm
//   bot.*    the bot view
//   reef.*   the coral round
//   womp.*   the womp frame
//   ota.*    the updater
//
// Writes hit flash, so write on change, never every frame.
namespace store {

constexpr size_t KEY_MAX = 15;

// Opens the namespace. Safe to call twice.
bool begin();
bool ready();

int32_t getInt(const char *key, int32_t fallback = 0);
bool setInt(const char *key, int32_t value);

float getFloat(const char *key, float fallback = 0.0f);
bool setFloat(const char *key, float value);

bool getBool(const char *key, bool fallback = false);
bool setBool(const char *key, bool value);

String getString(const char *key, const char *fallback = "");
bool setString(const char *key, const String &value);

bool has(const char *key);
bool remove(const char *key);

// Wipes every key in the namespace. WiFi credentials are compiled in, not
// stored here, so this cannot lock anyone out.
bool clear();

}  // namespace store
