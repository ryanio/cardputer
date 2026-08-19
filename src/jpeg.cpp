#include "jpeg.h"

#include <M5Cardputer.h>

#include "ui.h"

namespace jpeg {

bool draw(Stream &body, int length)
{
	(void)length;
	// The whole point of this file. The decoder asks the stream for the next
	// few bytes, pushes the block it just finished at the panel, and asks
	// again. Nothing bigger than its own work pool is ever held, which is the
	// only reason a 128KB photo fits in a device with 320KB of RAM and a TLS
	// session already inside it.
	return ui::gfx().drawJpg(&body, 0, 0, ui::W, ui::H, 0, 0, -1.0f, 0.0f, datum_t::middle_center);
}

}  // namespace jpeg
