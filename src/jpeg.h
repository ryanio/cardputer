#pragma once

#include <Arduino.h>

// Drawing a JPEG that is bigger than the RAM it would take to hold.
//
// Split from the rest of the drawing code the same way net.cpp is, because the
// two builds get here differently: the device hands M5GFX the socket and the
// decoder pulls from it, and the simulator cannot, so this is the one place
// where what you see on a desktop is not what the unit does.
namespace jpeg {

// Decode from a body that is still arriving, filling the panel across and
// keeping the aspect. Womps are square, so that crops the top and bottom
// rather than leaving a 135 pixel picture between two black bars.
//
// Returns false if the decoder refused the data, which on a device usually
// means the transfer was cut short rather than that the file was bad.
bool draw(Stream &body, int length);

}  // namespace jpeg
