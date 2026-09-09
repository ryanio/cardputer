#pragma once

// Which apps this build ships, and what the spine brings up for them.
//
// Every view stays in the tree and keeps working. A profile only decides which
// of them register, so a unit can be built as the whole of flint or as one
// app, from the same source, with no view edited or deleted to do it.
//
// The profile is chosen at build time, because the reason to want one is a
// unit that boots as an Anchor panel rather than a menu:
//
//   pio run -e cardputer-adv          every view, the default
//   pio run -e cardputer-adv-anchor   the Anchor panel, and no radio
//   pio run -e sim -t exec            every view, on the desktop
//   pio run -e sim-anchor -t exec     the Anchor panel, on the desktop
//
// A view never mentions a profile. view::add asks here, and a name no profile
// lists is never registered, so nothing it left out can be opened, shown, or
// asked to tick.
//
// What a profile is not is a way to make an image smaller. Every view is still
// compiled and linked: measured on the ADV target, the anchor profile came out
// 140 bytes larger than the full build, which is the table. A profile decides
// what a unit does, not what it carries.
//
// Adding a profile is one array and one row in the table in profile.cpp.
namespace view {
struct View;
}

namespace profile {

// The profile this build was compiled with: "full", "anchor".
const char *name();

// True when this build ships that view. Called once per view, before setup.
bool enabled(const view::View *v);

// True when the profile ships exactly one app. The spine boots straight into
// it rather than opening a menu with one card in it.
bool single();

// Whether the spine joins WiFi and runs its boot probe.
//
// Not every app reads the network, and a radio that nothing uses is attack
// surface with no upside. The Anchor panel is the case that made this a flag:
// its data comes down the USB cable from a desktop, by design, because the
// service behind it binds loopback and never the LAN.
bool network();

}  // namespace profile
