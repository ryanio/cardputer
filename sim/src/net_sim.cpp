#include <Arduino.h>

#include "net.h"
#include "store.h"

// The network, simulated. Credentials go through store like they do on the
// device, so Setup behaves the same: what you type is remembered, forgetting
// it works, and a passphrase under the WPA2 minimum fails the way a wrong one
// does. Fetches have no fixtures yet and say so rather than pretending.
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

}  // namespace

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

Result getJson(const char *url, JsonDocument &, JsonDocument *)
{
	Result r;
	r.status = online() ? ERR_TRANSPORT : ERR_NO_WIFI;
	Serial.printf("sim: no fixture for %s\n", url);
	return r;
}

Result get(const char *url, std::function<bool(Stream &, int)>)
{
	Result r;
	r.status = online() ? ERR_TRANSPORT : ERR_NO_WIFI;
	Serial.printf("sim: no fixture for %s\n", url);
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
