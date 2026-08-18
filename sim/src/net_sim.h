#pragma once

#include <cstdint>

// The one knob the simulated network has that the real one does not.
namespace net {

// Hold every fetch this long before it answers. A fixture returns instantly,
// which hides the screen a view draws while it waits: a Coral score takes
// seconds on a device. Set by --latency so a tour or a screenshot run can see
// the waiting state.
void simLatency(uint32_t ms);

}  // namespace net
