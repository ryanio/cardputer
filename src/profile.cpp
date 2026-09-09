#include "profile.h"

#include <string.h>

#include "view.h"

// The table. One array of view names per profile, and a null list meaning
// everything.
//
// Matching is by the view's menu name rather than by a symbol, because a view
// is registered from its own file and the spine never sees its type. A name
// that no longer exists simply matches nothing, which is the failure worth
// having: a profile that lists a deleted app ships one app fewer, rather than
// failing to link.

#ifndef FLINT_PROFILE
#define FLINT_PROFILE "full"
#endif

namespace profile {

namespace {

// Anchor alone. Setup is deliberately out: this build joins no network, so a
// screen for typing a passphrase would be a screen that does nothing. Add it
// back here the day an Anchor unit wants WiFi for something else.
const char *const ANCHOR[] = {"Anchor", nullptr};

struct Entry {
	const char *name;
	const char *const *views;  // null: every view registers
	bool network;
};

const Entry TABLE[] = {
    {"full", nullptr, true},
    {"anchor", ANCHOR, false},
};

const Entry &current()
{
	for (const Entry &entry : TABLE) {
		if (strcmp(entry.name, FLINT_PROFILE) == 0) {
			return entry;
		}
	}
	// An unknown name builds the whole firmware rather than an empty one. A
	// typo in a build flag should cost a puzzled look, not a blank unit.
	return TABLE[0];
}

}  // namespace

const char *name()
{
	return current().name;
}

bool enabled(const view::View *v)
{
	if (v == nullptr || v->name == nullptr) {
		return false;
	}
	const char *const *list = current().views;
	if (list == nullptr) {
		return true;
	}
	for (const char *const *want = list; *want != nullptr; want++) {
		if (strcmp(*want, v->name) == 0) {
			return true;
		}
	}
	return false;
}

bool single()
{
	const char *const *list = current().views;
	return list != nullptr && list[0] != nullptr && list[1] == nullptr;
}

bool network()
{
	return current().network;
}

}  // namespace profile
