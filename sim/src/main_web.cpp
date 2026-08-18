#include <emscripten.h>

#include <M5Cardputer.h>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include <cstdint>

#include "app.h"
#include "ui.h"

// The browser build, which differs from the desktop one in two ways.
//
// It has one thread. The SDL entry point runs the app on a second thread and
// the event loop on the first, and asking a browser for a thread needs cross
// origin isolation headers that GitHub Pages does not send. So the panel is
// pumped by hand from the page's own frame loop instead.
//
// And it does its own blitting. SDL's renderer holds a WebGL context on the
// canvas, and nothing it presented ever reached the page. Reading the panel's
// framebuffer and painting it into a 2D canvas is fewer moving parts, it is
// the same read the simulator's screenshots already use, and it gives the page
// a canvas at exactly 240x135 that CSS can scale by whole numbers.
namespace {

uint8_t pixels[ui::W * ui::H * 3];

void frame()
{
	simLoop();
	lgfx::Panel_sdl::loop();

	M5Cardputer.Display.readRectRGB(0, 0, ui::W, ui::H, pixels);
	// HEAPU8 is the module's own view and is in scope here, so the page never
	// has to reach into a module object that does not exist yet: main runs
	// before the loader's promise resolves.
	// clang-format off
	EM_ASM({
		if (window.coralBlit) { window.coralBlit(HEAPU8, $0, $1, $2); }
	}, pixels, ui::W, ui::H);
	// clang-format on
}

}  // namespace

int main(int argc, char **argv)
{
	simArgs(argc, argv);
	if (lgfx::Panel_sdl::setup() != 0) {
		return 1;
	}
	simSetup();
	emscripten_set_main_loop(frame, 0, 1);
	return 0;
}
