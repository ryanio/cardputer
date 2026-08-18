#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "ca_roots.h"
#include "store.h"
#include "version.h"

// A dev unit can compile its network in. Nothing else should: credentials
// typed on the device win over these, and a unit meant to be given away
// carries none of them. A fresh clone has to build without the file at all.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

namespace net {

namespace {

constexpr uint32_t JOIN_TIMEOUT_MS = 20000;
constexpr uint32_t RETRY_MS = 15000;
constexpr uint32_t CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t READ_TIMEOUT_MS = 12000;
constexpr int HANDSHAKE_TIMEOUT_S = 12;

// The Coral round turns over on the ET day, so the device keeps ET rather than
// UTC and localtime_r gives the boundary for free.
constexpr const char *TZ_ET = "EST5EDT,M3.2.0,M11.1.0/2";

constexpr const char *USER_AGENT = "cardputer-coral/" FW_VERSION;

// Any time after this is a clock that SNTP has actually set.
constexpr time_t CLOCK_SANE = 1735689600;  // 2025 01 01

Wifi st = Wifi::Off;
uint32_t joinStarted = 0;
uint32_t retryAt = 0;
bool clockOk = false;

// Keys in NVS. store.h reserves the sys prefix for the spine.
constexpr const char *SSID_KEY = "sys.ssid";
constexpr const char *PASS_KEY = "sys.pass";

String netSsid;
String netPass;
bool fromStore = false;

// NVS first, so a unit answers to whoever set it up last.
void loadCredentials()
{
	netSsid = store::getString(SSID_KEY, "");
	netPass = store::getString(PASS_KEY, "");
	fromStore = netSsid.length() > 0;
	if (!fromStore) {
		netSsid = WIFI_SSID;
		netPass = WIFI_PASSWORD;
	}
}

// Counts what the parser or the sink actually pulled, so a Result reports real
// bytes rather than an advertised content length.
class CountingStream : public Stream {
public:
	explicit CountingStream(Stream &source) : _source(source)
	{
		setTimeout(source.getTimeout());
	}

	int available() override
	{
		return _source.available();
	}

	int read() override
	{
		int c = _source.read();
		if (c >= 0) {
			_count++;
		}
		return c;
	}

	int peek() override
	{
		return _source.peek();
	}

	size_t readBytes(char *buffer, size_t length) override
	{
		size_t n = _source.readBytes(buffer, length);
		_count += n;
		return n;
	}

	size_t write(uint8_t b) override
	{
		return _source.write(b);
	}

	size_t write(const uint8_t *buffer, size_t size) override
	{
		return _source.write(buffer, size);
	}

	void flush() override
	{
		_source.flush();
	}

	size_t count() const
	{
		return _count;
	}

private:
	Stream &_source;
	size_t _count = 0;
};

void join()
{
	st = Wifi::Joining;
	joinStarted = millis();
	WiFi.begin(netSsid.c_str(), netPass.c_str());
	Serial.printf("net: joining \"%s\" (%s)\n", netSsid.c_str(),
	              fromStore ? "typed on the device" : "built in");
}

void onOnline()
{
	Serial.printf("net: online as %s, rssi %d dBm, heap %u free\n",
	              WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
	configTzTime(TZ_ET, "pool.ntp.org", "time.nist.gov", "time.google.com");
}

// HTTPClient reports its own transport failures as small negatives. Fold them
// into the two cases a view can do anything about.
int mapClientError(int code)
{
	switch (code) {
		case HTTPC_ERROR_CONNECTION_REFUSED:
		case HTTPC_ERROR_NOT_CONNECTED:
		case HTTPC_ERROR_NO_HTTP_SERVER:
			return ERR_CONNECT;
		default:
			return ERR_TRANSPORT;
	}
}

bool prepare(HTTPClient &http, WiFiClientSecure &client, const char *url, const char *accept)
{
	client.setCACert(CA_ROOTS);
	client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
	client.setTimeout(READ_TIMEOUT_MS / 1000);

	if (!http.begin(client, url)) {
		return false;
	}
	http.setConnectTimeout(CONNECT_TIMEOUT_MS);
	http.setTimeout(READ_TIMEOUT_MS);
	http.setUserAgent(USER_AGENT);
	http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
	http.setRedirectLimit(3);
	// HTTP/1.0 so the server answers with a plain body. Over 1.1 it is free to
	// chunk, and getStream() hands back the chunk framing along with the bytes,
	// which the JSON parser and a JPEG decoder both choke on.
	http.useHTTP10(true);
	if (accept != nullptr) {
		http.addHeader("Accept", accept);
	}
	return true;
}

bool ready(Result &r)
{
	if (netSsid.isEmpty()) {
		r.status = ERR_NO_CREDENTIALS;
		return false;
	}
	if (st != Wifi::Online) {
		r.status = ERR_NO_WIFI;
		return false;
	}
	return true;
}

void report(const char *url, const Result &r)
{
	Serial.printf("net: %s %s %u B in %u ms, heap %u free, %u low\n", statusText(r.status), url,
	              (unsigned)r.bytes, (unsigned)r.ms, (unsigned)ESP.getFreeHeap(),
	              (unsigned)ESP.getMinFreeHeap());
}

}  // namespace

void begin()
{
	loadCredentials();

	// The radio comes up either way. Scanning has to work on a device that has
	// never been told a network, because that is the one that needs Setup.
	WiFi.persistent(false);
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(true);
	WiFi.setSleep(true);

	if (netSsid.isEmpty()) {
		st = Wifi::Off;
		Serial.println("net: no network set, waiting for one from Setup");
		return;
	}
	join();
}

void loop()
{
	if (st == Wifi::Off) {
		return;
	}

	const bool linked = WiFi.status() == WL_CONNECTED;
	switch (st) {
		case Wifi::Joining:
			if (linked) {
				st = Wifi::Online;
				onOnline();
			} else if (millis() - joinStarted > JOIN_TIMEOUT_MS) {
				st = Wifi::Failed;
				retryAt = millis() + RETRY_MS;
				WiFi.disconnect();
				Serial.println("net: join failed, retrying shortly");
			}
			break;
		case Wifi::Failed:
			if ((int32_t)(millis() - retryAt) >= 0) {
				join();
			}
			break;
		case Wifi::Online:
			if (!linked) {
				Serial.println("net: link dropped, rejoining");
				st = Wifi::Joining;
				joinStarted = millis();
			}
			break;
		default:
			break;
	}

	if (!clockOk && st == Wifi::Online && time(nullptr) > CLOCK_SANE) {
		clockOk = true;
		time_t now = time(nullptr);
		struct tm tm;
		localtime_r(&now, &tm);
		Serial.printf("net: clock set, %04d %02d %02d %02d:%02d ET\n", tm.tm_year + 1900,
		              tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
	}
}

void reconnect()
{
	if (netSsid.isEmpty()) {
		return;
	}
	WiFi.disconnect();
	join();
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
	const bool ok = store::setString(SSID_KEY, String(ssid)) &&
	                store::setString(PASS_KEY, String(password == nullptr ? "" : password));
	loadCredentials();
	WiFi.disconnect();
	join();
	return ok;
}

void forgetCredentials()
{
	store::remove(SSID_KEY);
	store::remove(PASS_KEY);
	loadCredentials();
	WiFi.disconnect(true);
	if (netSsid.isEmpty()) {
		st = Wifi::Off;
		Serial.println("net: network forgotten");
	} else {
		Serial.println("net: network forgotten, falling back to the built in one");
		join();
	}
}

bool scanStart()
{
	if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
		return false;
	}
	WiFi.scanDelete();
	return WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING;
}

int scanCount()
{
	return WiFi.scanComplete();
}

String scanSsid(int index)
{
	return WiFi.SSID(index);
}

int32_t scanRssi(int index)
{
	return WiFi.RSSI(index);
}

bool scanOpen(int index)
{
	return WiFi.encryptionType(index) == WIFI_AUTH_OPEN;
}

void scanClear()
{
	WiFi.scanDelete();
}

int8_t rssi()
{
	return st == Wifi::Online ? (int8_t)WiFi.RSSI() : 0;
}

IPAddress ip()
{
	return WiFi.localIP();
}

bool clockSet()
{
	return clockOk;
}

Result getJson(const char *url, JsonDocument &doc, JsonDocument *filter)
{
	Result r;
	const uint32_t t0 = millis();
	if (!ready(r)) {
		r.ms = millis() - t0;
		return r;
	}

	WiFiClientSecure client;
	HTTPClient http;
	if (!prepare(http, client, url, "application/json")) {
		r.status = ERR_URL;
		r.ms = millis() - t0;
		return r;
	}

	const int code = http.GET();
	if (code <= 0) {
		r.status = mapClientError(code);
		r.ms = millis() - t0;
		http.end();
		report(url, r);
		return r;
	}
	if (code != HTTP_CODE_OK) {
		r.status = code;
		r.ms = millis() - t0;
		http.end();
		report(url, r);
		return r;
	}

	CountingStream body(http.getStream());
	DeserializationError err =
	    filter == nullptr
	        ? deserializeJson(doc, body)
	        : deserializeJson(doc, body, DeserializationOption::Filter(*filter));
	r.bytes = body.count();
	r.status = err ? ERR_PARSE : 200;
	r.ms = millis() - t0;
	if (err) {
		Serial.printf("net: parse failed, %s\n", err.c_str());
	}
	http.end();
	report(url, r);
	return r;
}

Result get(const char *url, std::function<bool(Stream &, int)> sink)
{
	Result r;
	const uint32_t t0 = millis();
	if (!ready(r)) {
		r.ms = millis() - t0;
		return r;
	}

	WiFiClientSecure client;
	HTTPClient http;
	if (!prepare(http, client, url, nullptr)) {
		r.status = ERR_URL;
		r.ms = millis() - t0;
		return r;
	}

	const int code = http.GET();
	if (code <= 0 || code != HTTP_CODE_OK) {
		r.status = code <= 0 ? mapClientError(code) : code;
		r.ms = millis() - t0;
		http.end();
		report(url, r);
		return r;
	}

	CountingStream body(http.getStream());
	const bool done = sink ? sink(body, http.getSize()) : false;
	r.bytes = body.count();
	r.status = done ? 200 : ERR_ABORTED;
	r.ms = millis() - t0;
	http.end();
	report(url, r);
	return r;
}

const char *caBundle()
{
	return CA_ROOTS;
}

const char *statusText(int status)
{
	switch (status) {
		case 200:
			return "ok";
		case ERR_NO_WIFI:
			return "no wifi";
		case ERR_NO_CREDENTIALS:
			return "no network set";
		case ERR_URL:
			return "bad url";
		case ERR_CONNECT:
			return "no connection";
		case ERR_TRANSPORT:
			return "transport failed";
		case ERR_PARSE:
			return "bad json";
		case ERR_ABORTED:
			return "aborted";
		case 304:
			return "unchanged";
		case 404:
			return "not found";
		case 429:
			return "rate limited";
		case 500:
		case 502:
		case 503:
		case 504:
			return "server down";
		default:
			break;
	}
	static char other[16];
	snprintf(other, sizeof(other), "http %d", status);
	return other;
}

}  // namespace net
