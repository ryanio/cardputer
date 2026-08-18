// Coral on the Cardputer ADV: a three-view app over gwei, glyphbots and coral.
// See docs/ROADMAP.md for what lands when.

#include <M5Cardputer.h>

void setup() {
	auto cfg = M5.config();
	M5Cardputer.begin(cfg, true);
	M5Cardputer.Display.setRotation(1);
	M5Cardputer.Display.setTextSize(2);
	M5Cardputer.Display.println("coral");
}

void loop() {
	M5Cardputer.update();
}
