#include "rest.h"

#include <M5Cardputer.h>

#include "motion.h"
#include "ui.h"
#include "view.h"

namespace rest {

namespace {

// Turned over and left alone. Long enough that flipping the unit to read the
// back of it does not count, short enough to be the thing you do when you are
// finished with it.
constexpr uint32_t SETTLE_MS = 2500;

bool sleeping = false;
uint8_t brightness = 0;

}  // namespace

void loop(bool activity)
{
	M5GFX &g = ui::gfx();

	if (!sleeping) {
		if (!motion::available() || !motion::faceDown() || motion::stillFor() < SETTLE_MS ||
		    activity) {
			return;
		}
		// Cleared before the backlight goes, so waking is not a flash of the
		// screen as it was two hours ago followed by the repaint.
		brightness = g.getBrightness();
		g.fillScreen(ui::BG);
		g.setBrightness(0);
		g.sleep();
		sleeping = true;
		return;
	}

	if (!activity && motion::faceDown()) {
		return;
	}
	g.wakeup();
	g.setBrightness(brightness);
	sleeping = false;
	view::repaint();
}

bool asleep()
{
	return sleeping;
}

}  // namespace rest
