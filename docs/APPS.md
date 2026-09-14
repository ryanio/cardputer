# Writing an app

An app here is a **view**: one file in `src/views/` that draws inside the body
and answers keys. The spine owns the menu, the exit convention, the status bar,
the keyboard and when to repaint, so a view is a screen and its data, and never
a program with a main loop of its own.

This is the contract. `src/view.h` is the header it is written down in.

## The file

```cpp
#include "../ui.h"
#include "../view.h"

namespace {

void draw()
{
	ui::clearBody();
	ui::title("Hello");
	ui::line(0, "a first row");
}

const view::View kHello = {
    .name = "Hello",     // the menu card
    .source = "GWEI",    // what the status bar names as the source of the numbers
    .order = 45,         // where it sits in the menu
    .icon = icons::WAVES,
    .draw = draw,
};

}  // namespace

VIEW_REGISTER(kHello);
```

That is a working app. Nothing else has to be edited: `VIEW_REGISTER` adds it
to the menu before `setup` runs, so no shared list, no header and no switch
statement grows a row. Two views landing at once never touch the same file.

## Lifecycle

Five hooks, all optional, all called from `view::loop` on the Arduino task:

| Hook | When | What belongs in it |
|---|---|---|
| `enter` | the view opens | take what you need, start a fetch, reset state |
| `tick` | every pass while it is open | polling, timers, reading a transport |
| `draw` | after `view::repaint()`, and once on open | paint the body, top to bottom |
| `key` | one key, once, on the pass it fired | move a cursor, page, type |
| `leave` | the view closes | give back sprites and buffers, cancel what is in flight |

Rules the loop enforces so a view cannot get them wrong:

- **Every view has an exit.** The backtick, and the G0 button, return to the
  menu before a view sees them. Fn and backtick together reach the view as a
  plain backtick, for text entry.
- **`draw` paints, `tick` decides.** `draw` runs when something asked for a
  repaint. Call `view::repaint()` when your data changed and let the loop pick
  the moment. The menu is the one screen that draws between repaints.
- **The bottom 13 rows are not yours.** `ui::STATUS_H` is the status bar, and
  the loop draws it with your `source` name in it. A view that wants the whole
  panel sets `fullScreen = true` and then owns the rule itself: it paints the
  source name into its own corner.
- **Ask for keys, never poll the keyboard.** `Keyboard.isChange()` is
  consuming: whoever calls it first is the only one told about a press. The
  loop is that caller, and it hands you a `view::Key` with the character, the
  four arrows, the modifiers and enter, tab and backspace already sorted out.

## What you get for free

| Seam | Header | What it is |
|---|---|---|
| Drawing | `ui.h` | the 240x135 layout, rows, cards, big numbers, icons, a glyph atlas, text trimming, ASCII folding |
| Colour | `ui.h` | the ten names every screen draws with, and `setPalette` to move all ten |
| Settings | `store.h` | typed key value pairs in NVS. Take a prefix, write on change |
| Input | `view.h` | one `Key` per pass, with the arrows already decoded |
| Motion | `motion.h` | tilt and shake, on settled axes. Never read `M5.Imu` directly |
| Network | `net.h`, `rest.h` | TLS, JSON with a filter so a payload never lands whole in RAM |
| Notes | `view::note` | a short line in the status bar: fetching, saved, no wifi |

An app is free to bring a transport of its own, and one has. Anchor's data
comes down the USB C cable from a desktop rather than from an API, because the
service behind it binds `127.0.0.1` and never the LAN: no credential exists
that a unit could hold, and holding one would be the wrong answer anyway. That
link, newline delimited JSON with a simulator half beside it, is a file in the
Anchor repository, not here. flint learned two things from hosting it before it
moved out, and both are in this file: `view::appBegin`, and the fact that a
transport is exactly the sort of thing an app pack should own.

## Profiles: which apps a build ships

Every view stays in the tree. A profile decides which ones register, so one
firmware can be the whole of flint or a single app appliance.

```bash
pio run -e cardputer-adv          # every view
```

`src/profile.cpp` holds the table: a profile is a name, a list of view names,
and whether the spine brings up WiFi. A profile with one app in it opens that
app at boot rather than a menu with one card, and the backtick still comes back
out to it. Adding a profile is one array and one row.

A profile is also declarable entirely in build flags, which is what an app pack
uses, because it cannot add a row to a table in this repository:

```
-DFLINT_PROFILE='"anchor"'
-DFLINT_PROFILE_VIEWS='"Anchor"'    # comma separated menu names
-DFLINT_PROFILE_NETWORK=0
```

`FLINT_PROFILE_VIEWS` is what tells the two apart. Define it and the table is
never consulted, so an app pack never has to know what is in it.

A profile is not a way to make the image smaller. Every view in the build is
still compiled and linked; measured on the ADV target a one app profile came
out 140 bytes larger than the full build, which is the table itself. An app
pack is the other lever and it does shrink the image, because flint's own views
are not in the build at all: the Anchor unit measures 33.5% of the app slot
against the full firmware's 37.7%.

An app never mentions a profile, and a profile never edits an app.

## An app in another repository

An **app pack** is a directory of views in someone else's tree, built against
flint rather than added to it. Anchor is the first one, and the reason it
exists is not technical: somebody who installs Anchor should not find ten
unrelated flint apps on the unit, and somebody reading flint should not find
Anchor's wire protocol in it. Each repository holds what it owns.

Four things make it work, and none of them is Anchor shaped.

**1. flint.ini.** The board, the libraries and the flags live there rather than
in `platformio.ini`, so a project that vendors flint as a submodule gets them
by including one file and cannot drift from them. It defines `flint_adv` and
`flint_sim` as plain sections, not environments, so an including project does
not inherit two builds it never asked for. Read the header of that file: it is
the reference for the two layout rules, of which the load bearing one is that
`src_dir` is the project root, because a source filter that climbs out of
`src_dir` with `..` puts its objects in a directory every environment shares.

**2. The profile flags above**, which say which views ship and whether a radio
comes up, without either repository editing the other.

**3. `view::appBegin`.** Views register themselves, so an app whose views only
draw needs no hook at all. An app that owns a transport, a fixture or a store
prefix needs somewhere to start it and cannot edit `main.cpp`, so define this
and `view::begin` calls it once, after every view has registered and before the
first one opens. It is weak: a build with no app pack links exactly as before.

**4. `ui::setPalette`.** An app that themes only its own screen is a themed
panel sitting inside somebody else's chrome: the menu it was opened from, the
status bar above it and any other view in the build stay flint's coral on black
whatever the app is wearing. So the ten colours are a palette rather than ten
constants, and an app pack can move all ten at once:

```cpp
ui::Palette p;            // flint's own scheme, or ui::palette() for what is up now
p.bg = ui::rgb565(28, 20, 40);
p.accent = ui::rgb565(126, 226, 168);   // drawn as ui::CORAL
ui::setPalette(p);                      // menu, status bar and every view follow
```

Set it whole, not a colour at a time: a screen half in your scheme and half in
flint's reads as a bug in whichever half the reader was not expecting. Call it
from `appBegin` for a scheme the app knows at boot, or from `tick` whenever the
app learns a new one; `setPalette` asks for the repaint itself, and drops a
palette equal to the current one so an app told its theme in every message it
receives does not repaint on every message.

Where the colours came from is not flint's business and nothing here asks. The
names themselves do not change: a view still writes `ui::CORAL`, which is a
variable now rather than a constant, so it is a load in a draw loop and not a
call, and a build that never calls `setPalette` draws exactly what flint drew
before the palette existed.

An app pack also has no id in the generated icon atlas, so it carries its own
art. `view::View::art` takes an `icons::Icon` directly, and the same generator
writes one:

```bash
python3 tools/icons/generate.py --pack anchor:ANCHOR:32 \
    --namespace anchorart --out ../app/src/art.h
```

The whole of the consuming side is one `platformio.ini`. Anchor's is at
`devices/firmware/cardputer/platformio.ini` in `ryanio/anchor`, and it is
worth reading before writing a second one.

## Seeing it without hardware

The simulator runs this same view, ui and store code against M5GFX's SDL panel
at the real 240x135, so a new app works there the day it is written. There is
nothing to register: the sim builds `src/views/` as it stands, and an app pack
adds its own directory to the filter.

```bash
pio run -e sim -t exec            # a window you can drive, every view in it
```

As a test harness it writes a PPM before each scripted key, which is how a
screen gets checked without anybody watching a window:

```bash
.pio/build/sim/program --keys "aaa" --shot /tmp/frame --quit-after 9000
```

The first shot fires at 700ms and one every 420ms after, on scripted keys, so
`--shot` with no `--keys` photographs the first screen and nothing else.

What is faked is the ring around the firmware: keys come from SDL, the battery
is a number, the IMU is the mouse, and fetches answer from captures in
`sim/fixtures`. A profile that brings no radio up brings none up here either,
the same as on a unit. Every capture is real bytes off the real source, because
a fixture that was typed by hand agrees with whatever the code happens to do,
which is the one thing a fixture must never do. An app pack that brings its own
transport brings its own capture too, and swaps the file the same way this does.

## What adding an app taught this file

The Anchor panel was the first app written against this contract from outside,
and each thing that was awkward became a change here rather than a workaround
there:

- **`ui::clip`** is public now. Anything drawing inside a card of its own needs
  text trimmed to a box in a font and colours it chose, and three views had
  each written their own copy of the same loop.
- **`ui::asciify` folds an em dash to a hyphen.** It used to drop it. Anchor
  writes one where it has no reading, so dropping it turned "no answer" into an
  empty space, which is a different claim and a worse one.
- **A host link exists at all.** An app whose data comes from a desktop had
  nowhere to read from, and the answer was a transport beside `net`, with a
  simulator half, rather than a view holding a serial port open. It lived here
  first and now lives with the app, which is the right home for a protocol only
  one program speaks.
- **Profiles exist at all.** A unit meant to be one app had no way to be one
  without deleting the other ten.
- **App packs exist at all.** A profile answered which of flint's apps ship. It
  could not answer where an app lives, and that turned out to be the question.
