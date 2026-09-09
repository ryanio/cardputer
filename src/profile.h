#pragma once

// Which apps this build ships, and what the spine brings up for them.
//
// Every view stays in the tree and keeps working. A profile only decides which
// of them register, so a unit can be built as the whole of flint or as one
// app, from the same source, with no view edited or deleted to do it.
//
// The profile is chosen at build time, because the reason to want one is a
// unit that boots as one app rather than a menu:
//
//   pio run -e cardputer-adv          every view, the default
//   pio run -e sim -t exec            every view, on the desktop
//
// A view never mentions a profile. view::add asks here, and a name no profile
// lists is never registered, so nothing it left out can be opened, shown, or
// asked to tick.
//
// A profile can come from the table in profile.cpp, or entirely from build
// flags, which is what an app pack in another repository uses:
//
//   -DFLINT_PROFILE='"anchor"'
//   -DFLINT_PROFILE_VIEWS='"Anchor"'
//   -DFLINT_PROFILE_NETWORK=0
//
// What a profile is not is a way to make an image smaller. Every view in the
// build is still compiled and linked: measured on the ADV target, a one app
// profile came out 140 bytes larger than the full build, which is the table. A
// profile decides what a unit does, not what it carries. An app pack is the
// other lever, and it does make the image smaller, because it leaves flint's
// views out of the build entirely. See docs/APPS.md.
namespace view {
struct View;
}

namespace profile {

// The profile this build was compiled with: "full", or whatever an app pack
// named itself.
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
