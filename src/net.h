#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <functional>

// WiFi and HTTPS. Every view fetches through here so there is one place that
// knows about the CA roots, the timeouts and the user agent.
//
// Both calls block for as long as the request takes, which on this device is
// up to a few seconds for a TLS handshake. Call them from a view's tick, not
// from a key handler, and respect the poll windows in CLAUDE.md: gwei refreshes
// at most every 30s, the Coral round is one fetch per ET day, and /score is
// request and response with a spinner, never a poll.
namespace net {

enum class Wifi : uint8_t {
	Off,      // no credentials, or the radio is down
	Joining,  // association in progress
	Online,   // associated, with an address
	Failed,   // gave up, will retry on a backoff
};

// Result::status carries an HTTP status when the request reached a server, or
// one of these when it did not.
constexpr int ERR_NO_WIFI = -1;
constexpr int ERR_NO_CREDENTIALS = -2;
constexpr int ERR_URL = -3;
constexpr int ERR_CONNECT = -4;
constexpr int ERR_TRANSPORT = -5;
constexpr int ERR_PARSE = -6;
constexpr int ERR_ABORTED = -7;

struct Result {
	int status = ERR_NO_WIFI;
	size_t bytes = 0;  // body bytes read
	uint32_t ms = 0;   // wall clock for the whole request
	bool ok() const
	{
		return status == 200;
	}
};

// Brings the radio up and starts the join if there is a network to join.
// Returns immediately: the menu stays usable while it associates.
void begin();

// Pumps the join state machine and the clock. Call every loop.
void loop();

// Drops the association and joins again. For a settings view.
void reconnect();

Wifi state();
bool online();

// The network the device joins, empty when none is set yet.
const char *ssid();

// Credentials live in NVS, typed on the device, so a unit can be handed to
// someone else without a rebuild and without carrying the last owner's
// network. include/secrets.h is only a fallback for a dev unit, and a unit you
// give away should have none compiled in at all.
bool haveCredentials();
bool credentialsAreStored();  // false means they came from secrets.h
bool saveCredentials(const char *ssid, const char *password);
void forgetCredentials();

// Async scan, so the view keeps drawing while it runs. scanCount returns -1
// while the scan is running, -2 when it failed, otherwise the number found.
bool scanStart();
int scanCount();
String scanSsid(int index);
int32_t scanRssi(int index);
bool scanOpen(int index);  // no passphrase needed
void scanClear();
int8_t rssi();
IPAddress ip();

// True once SNTP has landed. Time is kept in ET, so localtime_r gives the day
// boundary the Coral round runs on.
bool clockSet();

// GET a JSON body straight through the parser, so the whole response never
// exists in RAM at once. Pass a filter to keep only the fields a view reads:
// on this device that is the difference between a payload that fits and one
// that does not.
Result getJson(const char *url, JsonDocument &doc, JsonDocument *filter = nullptr);

// GET with the body handed to a sink as it arrives, for anything too big to
// hold: the womp JPEGs are around 128KB against 320KB of RAM with TLS already
// inside it, so they decode block by block straight to the panel.
// The sink gets the body stream and the content length (-1 when unknown), and
// returns false to abort the transfer.
Result get(const char *url, std::function<bool(Stream &body, int contentLength)> sink);

// Both root CAs as one PEM buffer, for code that runs its own TLS client.
// esp_https_ota wants this.
const char *caBundle();

// Human readable form of a Result::status, for a status bar or a log line.
const char *statusText(int status);

}  // namespace net
