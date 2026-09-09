#include "profile.h"

#include <stdio.h>
#include <string.h>

#include "view.h"

// The table, and the build flags that let a build outside this tree declare a
// profile of its own.
//
// In tree: one array of view names per profile, and a null list meaning
// everything. Matching is by the view's menu name rather than by a symbol,
// because a view is registered from its own file and the spine never sees its
// type. A name that no longer exists simply matches nothing, which is the
// failure worth having: a profile that lists a deleted app ships one app
// fewer, rather than failing to link.
//
// Out of tree: an app pack cannot add a row to the table below, because the
// table is in this repository and the app is not. So a profile can also be
// declared entirely in build flags:
//
//   -DFLINT_PROFILE='"anchor"'          the name, for the boot line
//   -DFLINT_PROFILE_VIEWS='"Anchor"'    comma separated menu names
//   -DFLINT_PROFILE_NETWORK=0           whether the spine brings a radio up
//
// FLINT_PROFILE_VIEWS is what tells the two apart: define it and the table is
// not consulted at all, so an app pack never has to know what is in it.

#ifndef FLINT_PROFILE
#define FLINT_PROFILE "full"
#endif

namespace profile {

namespace {

struct Entry {
	const char *name;
	const char *const *views;  // null: every view registers
	bool network;
};

#ifdef FLINT_PROFILE_VIEWS

#ifndef FLINT_PROFILE_NETWORK
#define FLINT_PROFILE_NETWORK 1
#endif

// A build flag profile. The list arrives as one string, so it is split once,
// in place, into a table of pointers the rest of this file reads like any
// other. Sixteen is well past what a single unit is for.
constexpr int MAX_VIEWS = 16;
char names[256];
const char *split[MAX_VIEWS + 1] = {nullptr};
bool ready = false;

void parse()
{
	if (ready) {
		return;
	}
	ready = true;
	snprintf(names, sizeof(names), "%s", FLINT_PROFILE_VIEWS);
	int found = 0;
	char *rest = names;
	while (rest != nullptr && *rest != '\0' && found < MAX_VIEWS) {
		char *comma = strchr(rest, ',');
		if (comma != nullptr) {
			*comma = '\0';
		}
		// A list written with a space after each comma is the obvious way to
		// write one, so it has to mean what it looks like.
		while (*rest == ' ') {
			rest++;
		}
		if (*rest != '\0') {
			split[found++] = rest;
		}
		rest = comma == nullptr ? nullptr : comma + 1;
	}
	split[found] = nullptr;
}

const Entry &current()
{
	parse();
	static const Entry entry = {FLINT_PROFILE, split, FLINT_PROFILE_NETWORK != 0};
	return entry;
}

#else

const Entry TABLE[] = {
    {"full", nullptr, true},
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

#endif

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
