#include <SDL2/SDL.h>

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include "app.h"

// On a desktop the SDL panel owns the event loop on the main thread and runs
// the app on another one.
namespace {

int userFunction(bool *running)
{
	simSetup();
	while (*running) {
		simLoop();
	}
	return 0;
}

}  // namespace

int main(int argc, char **argv)
{
	simArgs(argc, argv);
#if defined(__APPLE__)
	SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
#endif
	return lgfx::Panel_sdl::main(userFunction, 128);
}
