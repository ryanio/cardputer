#include <Arduino.h>

#include <cstring>

#include "fixtures.h"
#include "net.h"
#include "net_sim.h"
#include "store.h"

// The network, simulated. Credentials go through store like they do on the
// device, so Setup behaves the same: what you type is remembered, forgetting
// it works, and a passphrase under the WPA2 minimum fails the way a wrong one
// does.
//
// Fetches answer from the captures in sim/fixtures, matched by URL. A URL with
// no fixture fails rather than inventing a body, so a view's error state gets
// exercised and a demo never shows one token's numbers under another token's
// name.
namespace net {

namespace {

constexpr const char *SSID_KEY = "sys.ssid";
constexpr const char *PASS_KEY = "sys.pass";
constexpr uint32_t JOIN_MS = 1400;
constexpr uint32_t SCAN_MS = 900;

struct FakeNetwork {
	const char *ssid;
	int32_t rssi;
	bool open;
};

const FakeNetwork NETWORKS[] = {
    {"parcel-of-rogues", -41, false}, {"Voxels Guest", -58, true},
    {"reef", -63, false},             {"BT-HUB-8891", -71, false},
    {"eduroam", -77, false},          {"nowhere-fast", -84, false},
};
constexpr int NETWORK_COUNT = sizeof(NETWORKS) / sizeof(NETWORKS[0]);

uint32_t latency = 0;  // --latency, so a slow source can be watched being slow

Wifi st = Wifi::Off;
String netSsid;
String netPass;
bool fromStore = false;
uint32_t joinAt = 0;
uint32_t scanDoneAt = 0;
bool scanning = false;

void loadCredentials()
{
	netSsid = store::getString(SSID_KEY, "");
	netPass = store::getString(PASS_KEY, "");
	fromStore = !netSsid.isEmpty();
}

bool passphraseWorks()
{
	for (int i = 0; i < NETWORK_COUNT; i++) {
		if (netSsid == NETWORKS[i].ssid && NETWORKS[i].open) {
			return true;
		}
	}
	// WPA2 wants eight characters. Anything shorter is the wrong passphrase,
	// which gives the join failure path something to fail on.
	return netPass.length() >= 8;
}

// The manifest's * stands for any run of characters, and nothing else is
// special. Small enough to read, which matters more here than speed.
bool matches(const char *pattern, const char *text)
{
	if (*pattern == '\0') {
		return *text == '\0';
	}
	if (*pattern == '*') {
		for (const char *at = text;; at++) {
			if (matches(pattern + 1, at)) {
				return true;
			}
			if (*at == '\0') {
				return false;
			}
		}
	}
	return *text != '\0' && *pattern == *text && matches(pattern + 1, text + 1);
}

// Fixtures are keyed without a scheme, the way the manifest reads.
const char *bare(const char *url)
{
	const char *sep = strstr(url, "://");
	return sep == nullptr ? url : sep + 3;
}

const fixtures::Route *route(const char *url)
{
	const char *key = bare(url);
	for (int i = 0; i < fixtures::COUNT; i++) {
		if (matches(fixtures::ROUTES[i].match, key)) {
			return &fixtures::ROUTES[i];
		}
	}
	return nullptr;
}

// A fixture is instant, and a real fetch is not. Blocking here is faithful:
// net::getJson blocks on the device too, so whatever a view painted before it
// called is what stays on screen while the request runs.
void spend(Result &r, uint32_t started)
{
	if (latency != 0) {
		delay(latency);
	}
	r.ms = millis() - started;
}

// The sink in net::get wants a Stream, and a fixture is already in memory.
class MemStream : public Stream {
public:
	MemStream(const char *data, size_t size) : data_(data), size_(size) {}

	int available() override
	{
		return (int)(size_ - at_);
	}

	int read() override
	{
		return at_ < size_ ? (uint8_t)data_[at_++] : -1;
	}

	size_t readBytes(char *into, size_t want) override
	{
		const size_t left = size_ - at_;
		const size_t take = want < left ? want : left;
		memcpy(into, data_ + at_, take);
		at_ += take;
		return take;
	}

private:
	const char *data_;
	size_t size_;
	size_t at_ = 0;
};

}  // namespace

void simLatency(uint32_t ms)
{
	latency = ms;
}

void begin()
{
	loadCredentials();
	if (netSsid.isEmpty()) {
		st = Wifi::Off;
		Serial.println("net: no network set, waiting for one from Setup");
		return;
	}
	st = Wifi::Joining;
	joinAt = millis() + JOIN_MS;
	Serial.printf("net: joining \"%s\" (%s)\n", netSsid.c_str(),
	              fromStore ? "typed on the device" : "built in");
}

void loop()
{
	if (st == Wifi::Joining && millis() >= joinAt) {
		if (passphraseWorks()) {
			st = Wifi::Online;
			Serial.println("net: online as 192.168.1.50, rssi -47 dBm");
		} else {
			st = Wifi::Failed;
			Serial.println("net: join failed, the passphrase was not accepted");
		}
	}
}

void reconnect()
{
	if (netSsid.isEmpty()) {
		return;
	}
	st = Wifi::Joining;
	joinAt = millis() + JOIN_MS;
}

Wifi state()
{
	return st;
}

bool online()
{
	return st == Wifi::Online;
}

const char *ssid()
{
	return netSsid.c_str();
}

int8_t rssi()
{
	return st == Wifi::Online ? -47 : 0;
}

IPAddress ip()
{
	return st == Wifi::Online ? IPAddress(192, 168, 1, 50) : IPAddress();
}

bool clockSet()
{
	return st == Wifi::Online;
}

bool haveCredentials()
{
	return !netSsid.isEmpty();
}

bool credentialsAreStored()
{
	return fromStore;
}

bool saveCredentials(const char *ssid, const char *password)
{
	if (ssid == nullptr || strlen(ssid) == 0) {
		return false;
	}
	store::setString(SSID_KEY, String(ssid));
	store::setString(PASS_KEY, String(password == nullptr ? "" : password));
	loadCredentials();
	reconnect();
	return true;
}

void forgetCredentials()
{
	store::remove(SSID_KEY);
	store::remove(PASS_KEY);
	loadCredentials();
	st = netSsid.isEmpty() ? Wifi::Off : Wifi::Joining;
	Serial.println("net: network forgotten");
}

bool scanStart()
{
	if (scanning) {
		return false;
	}
	scanning = true;
	scanDoneAt = millis() + SCAN_MS;
	return true;
}

int scanCount()
{
	if (scanning && millis() < scanDoneAt) {
		return -1;
	}
	scanning = false;
	return NETWORK_COUNT;
}

String scanSsid(int index)
{
	return index >= 0 && index < NETWORK_COUNT ? String(NETWORKS[index].ssid) : String("");
}

int32_t scanRssi(int index)
{
	return index >= 0 && index < NETWORK_COUNT ? NETWORKS[index].rssi : 0;
}

bool scanOpen(int index)
{
	return index >= 0 && index < NETWORK_COUNT ? NETWORKS[index].open : false;
}

void scanClear()
{
	scanning = false;
}

Result getJson(const char *url, JsonDocument &doc, JsonDocument *filter)
{
	Result r;
	const uint32_t started = millis();
	if (!online()) {
		r.status = ERR_NO_WIFI;
		return r;
	}
	const fixtures::Route *hit = route(url);
	if (hit == nullptr) {
		r.status = ERR_TRANSPORT;
		spend(r, started);
		Serial.printf("sim: no fixture for %s\n", url);
		return r;
	}

	// Same parser and the same filter the device runs, so a filter that drops
	// a field a view needs fails here exactly as it would there.
	DeserializationError err =
	    filter == nullptr
	        ? deserializeJson(doc, hit->body, hit->size)
	        : deserializeJson(doc, hit->body, hit->size, DeserializationOption::Filter(*filter));
	r.bytes = hit->size;
	r.status = err ? ERR_PARSE : 200;
	spend(r, started);
	Serial.printf("sim: %s -> %s, %u bytes, %ums\n", url, hit->name, (unsigned)r.bytes,
	              (unsigned)r.ms);
	return r;
}

Result get(const char *url, std::function<bool(Stream &, int)> sink)
{
	Result r;
	const uint32_t started = millis();
	if (!online()) {
		r.status = ERR_NO_WIFI;
		return r;
	}
	const fixtures::Route *hit = route(url);
	if (hit == nullptr) {
		r.status = ERR_TRANSPORT;
		spend(r, started);
		Serial.printf("sim: no fixture for %s\n", url);
		return r;
	}

	MemStream body(hit->body, hit->size);
	const bool kept = sink(body, (int)hit->size);
	r.bytes = hit->size;
	r.status = kept ? 200 : ERR_ABORTED;
	spend(r, started);
	return r;
}

const char *caBundle()
{
	return "";
}

const char *statusText(int status)
{
	switch (status) {
		case 200: return "ok";
		case ERR_NO_WIFI: return "no wifi";
		case ERR_NO_CREDENTIALS: return "no network set";
		case ERR_URL: return "bad url";
		case ERR_CONNECT: return "no connection";
		case ERR_TRANSPORT: return "no fixture";
		case ERR_PARSE: return "bad json";
		case ERR_ABORTED: return "aborted";
		default: return "http error";
	}
}

}  // namespace net
