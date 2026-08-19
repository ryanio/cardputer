#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>

// The BMI270, turned into the three things a view actually asks: which way the
// unit is leaning, whether somebody shook it, and whether it is lying face
// down.
//
// One place does the filtering so that no view writes its own. Raw samples are
// noisy enough that a plain threshold fires on a keystroke, and a plain tilt
// wanders a degree or two at rest, which on a 240px panel reads as a twitch.
//
// M5Unified brings the IMU up inside M5Cardputer.begin, so nothing here
// initialises hardware. It does have to be polled: update() once a loop. The
// simulator answers from the mouse, so a view built on this can be looked at
// without a unit.
namespace motion {

// Whether an IMU answers at all. False on the original Cardputer, and false if
// M5Unified's probe missed it, so a view asks rather than assumes.
bool available();

// Once a loop. It samples at its own rate, so calling it more often than that
// costs one comparison.
void update();

// Lean in degrees, smoothed. Roll runs along the keyboard: positive tips the
// right hand edge down. Pitch runs across it: positive tips the top edge away.
float roll();
float pitch();

// The same lean as -1 to 1 with a dead zone around flat, which is what a view
// steering something wants. FULL_TILT degrees reaches the end of the range.
constexpr float FULL_TILT = 32.0f;
float steerX();
float steerY();

// Gravity as the sensor reports it, in g, after the axis fix. For a view
// drawing something that falls.
float gravityX();
float gravityY();
float gravityZ();

// One shot: reading it clears it. A shake is a deliberate rattle, so it takes
// two swings to fire and will not come from putting the unit down.
bool shaken();

// Face down on a desk, which is the one gesture worth acting on without asking
// a view: see rest.h.
bool faceDown();

// How long the unit has been completely still, in milliseconds.
uint32_t stillFor();

}  // namespace motion
