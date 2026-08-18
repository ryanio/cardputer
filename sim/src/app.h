#pragma once

// The simulator's app, split from the two platform entry points: SDL on a
// desktop, and the browser's own frame loop under emscripten, which cannot
// spawn the thread the SDL entry point wants.
void simArgs(int argc, char **argv);
void simSetup();
void simLoop();

// Pressing a key from outside, for the guided tour, for scripted tests, and
// for the buttons under the canvas on the web.
extern "C" void simPress(int key);
