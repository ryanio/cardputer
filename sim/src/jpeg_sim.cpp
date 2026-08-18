#include <M5Cardputer.h>

#include <vector>

#include "jpeg.h"
#include "ui.h"

// M5GFX only compiles its drawJpg(Stream*) overloads when it believes it is on
// Arduino, and making the simulator claim that swaps its whole panel driver for
// the ESP32's SPI bus. So the desktop reads the body first and hands the
// decoder a buffer.
//
// This is the one place the simulator is not the firmware. What it still
// checks is everything after the decode: the fit, the crop, the caption and
// the browsing. Whether a 128KB stream decodes inside 320KB of RAM beside a
// TLS session is a question only the device answers, and it is written down as
// unknown 3 in docs/PLAN.md.
namespace jpeg {

bool draw(Stream &body, int length)
{
	std::vector<uint8_t> all;
	if (length > 0) {
		all.reserve((size_t)length);
	}
	uint8_t chunk[1024];
	size_t got = 0;
	while ((got = body.readBytes(chunk, sizeof(chunk))) > 0) {
		all.insert(all.end(), chunk, chunk + got);
	}
	if (all.empty()) {
		return false;
	}
	return ui::gfx().drawJpg(all.data(), all.size(), 0, 0, ui::W, ui::H, 0, 0, -1.0f, 0.0f,
	                         datum_t::middle_center);
}

}  // namespace jpeg
