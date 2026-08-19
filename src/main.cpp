// Coral on the Cardputer ADV: four views over gwei, glyphbots, coral and
// voxels. See docs/ROADMAP.md for what lands when.

#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <WiFi.h>

#include "motion.h"
#include "net.h"
#include "rest.h"
#include "store.h"
#include "ui.h"
#include "version.h"
#include "view.h"

namespace {

// One request at boot against the smallest payload of the four sources. It
// answers the question the rest of the firmware rests on: whether a TLS
// handshake fits in 320KB beside the display buffer. gwei refreshes at most
// every 30s and this runs once per boot, so it costs the server nothing.
constexpr const char *PROBE_URL = "https://gwei.ryanio.com/api/gas";

bool probed = false;

void bootReport()
{
	Serial.printf("\nflint %s, built %s %s\n", FW_VERSION, __DATE__, __TIME__);
	Serial.printf("chip %s rev %d, %d cores at %u MHz\n", ESP.getChipModel(),
	              (int)ESP.getChipRevision(), (int)ESP.getChipCores(),
	              (unsigned)getCpuFrequencyMhz());
	Serial.printf("flash %u KB, sketch %u KB of %u KB\n", (unsigned)(ESP.getFlashChipSize() / 1024),
	              (unsigned)(ESP.getSketchSize() / 1024),
	              (unsigned)((ESP.getSketchSize() + ESP.getFreeSketchSpace()) / 1024));
	Serial.printf("mac %s\n", WiFi.macAddress().c_str());

	// The two lines the Stage 1 gate exists for. PSRAM support is compiled in,
	// so psramFound is a real probe of the hardware and not a build setting:
	// none here means the womp decode has to stream.
	if (psramFound()) {
		Serial.printf("psram yes, %u KB total, %u KB free\n", (unsigned)(ESP.getPsramSize() / 1024),
		              (unsigned)(ESP.getFreePsram() / 1024));
	} else {
		Serial.println("psram none");
	}
	// Which way up the BMI270 is glued is the one thing the axis constants in
	// motion.cpp cannot be reasoned out, so the raw sample is printed here.
	// Face up on a desk it should read near 0, 0, +1. Whichever number is not
	// where this says it should be names the sign to flip.
	if (motion::available()) {
		float ax = 0.0f;
		float ay = 0.0f;
		float az = 0.0f;
		M5.Imu.getAccel(&ax, &ay, &az);
		Serial.printf("imu yes, accel %.2f %.2f %.2f g\n", ax, ay, az);
	} else {
		Serial.println("imu none");
	}

	Serial.printf("heap %u KB free of %u KB, largest block %u KB\n",
	              (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024),
	              (unsigned)(ESP.getMaxAllocHeap() / 1024));
}

void bootCard()
{
	char text[40];
	ui::clearBody();
	ui::title("flint " FW_VERSION);

	snprintf(text, sizeof(text), "heap  %u KB free", (unsigned)(ESP.getFreeHeap() / 1024));
	ui::line(0, text);
	snprintf(text, sizeof(text), "psram %s", psramFound() ? "yes" : "none");
	ui::line(1, text, psramFound() ? ui::GOOD : ui::WARN);
	snprintf(text, sizeof(text), "flash %u KB", (unsigned)(ESP.getFlashChipSize() / 1024));
	ui::line(2, text);
	snprintf(text, sizeof(text), "imu   %s", motion::available() ? "yes" : "none");
	ui::line(3, text, motion::available() ? ui::GOOD : ui::DIM);
	ui::line(4, net::haveCredentials() ? "joining wifi" : "wifi: open Setup", ui::DIM);
	ui::statusBar("flint " FW_VERSION);
}

void probe()
{
	// Only one field is read, so the filter keeps the rest of the payload out
	// of RAM entirely. Every view fetches this way.
	JsonDocument filter;
	filter["baseFeeGwei"] = true;

	JsonDocument doc;
	const uint32_t before = ESP.getFreeHeap();
	const net::Result r = net::getJson(PROBE_URL, doc, &filter);
	const uint32_t after = ESP.getFreeHeap();

	Serial.printf("probe: tls %s, heap %u KB before, %u KB after, %u KB low water\n",
	              net::statusText(r.status), (unsigned)(before / 1024), (unsigned)(after / 1024),
	              (unsigned)(ESP.getMinFreeHeap() / 1024));
	if (r.ok()) {
		Serial.printf("probe: base fee %.4f gwei\n", doc["baseFeeGwei"].as<float>());
	}

	char text[32];
	snprintf(text, sizeof(text), "net %s", net::statusText(r.status));
	view::note(text);
}

}  // namespace

void setup()
{
	auto cfg = M5.config();
	M5Cardputer.begin(cfg, true);
	Serial.begin(115200);

	ui::begin();
	store::begin();
	bootReport();
	net::begin();
	view::begin();

	// Long enough to read the heap and PSRAM lines without a serial cable.
	bootCard();
	delay(2000);
	view::repaint();
}

void loop()
{
	M5Cardputer.update();

	motion::update();
	// isPressed rather than isChange: isChange is consuming. It compares the
	// key count against the last time anybody asked and updates that count as
	// it answers, so the first caller in a pass is the only one who hears
	// about a keypress. view::loop is the one that has to hear it, so nothing
	// else on the way there is allowed to ask. A key being held is all this
	// needs to know anyway.
	rest::loop(M5Cardputer.Keyboard.isPressed() != 0);

	net::loop();

	if (!probed && net::online()) {
		probed = true;
		probe();
	}

	view::loop();
	delay(5);
}
