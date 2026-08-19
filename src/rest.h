#pragma once

#include <Arduino.h>

// Face down means put down.
//
// The unit lives on the side of a monitor with a magnet and a 1750mAh battery,
// and the panel is most of what drains it. Turning it over is a gesture nobody
// has to be taught, it cannot be triggered by accident while the thing is
// being read, and picking it up puts the screen back. It is the whole power
// policy on the device.
//
// Nothing else stops: a view keeps ticking and a poll keeps polling while the
// panel is dark, so this never changes what the firmware is doing, only
// whether anyone can see it.
namespace rest {

// Once a loop, after motion::update. `activity` is whether a key or the button
// was touched this pass, which wakes it even face down.
void loop(bool activity);

bool asleep();

}  // namespace rest
