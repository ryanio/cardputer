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
| Settings | `store.h` | typed key value pairs in NVS. Take a prefix, write on change |
| Input | `view.h` | one `Key` per pass, with the arrows already decoded |
| Motion | `motion.h` | tilt and shake, on settled axes. Never read `M5.Imu` directly |
| Network | `net.h`, `rest.h` | TLS, JSON with a filter so a payload never lands whole in RAM |
| Host link | `cable.h` | newline delimited JSON over the USB C cable, for an app whose data comes from a desktop rather than an API |
| Notes | `view::note` | a short line in the status bar: fetching, saved, no wifi |

Two of these are transports and the choice between them is the design
question, not a preference. `net` reaches a public API over WiFi. `cable` reads
a program on the other end of the cable. The Anchor panel uses `cable` because
its data service binds `127.0.0.1` and never the LAN: no credential exists that
a unit could hold, and holding one would be the wrong answer anyway.

## Profiles: which apps a build ships

Every view stays in the tree. A profile decides which ones register, so one
firmware can be the whole of flint or a single app appliance.

```bash
pio run -e cardputer-adv          # every view
pio run -e cardputer-adv-anchor   # the Anchor panel, and no radio
```

`src/profile.cpp` holds the table: a profile is a name, a list of view names,
and whether the spine brings up WiFi. A profile with one app in it opens that
app at boot rather than a menu with one card, and the backtick still comes back
out to it. Adding a profile is one array and one row.

It is not a way to make the image smaller. Every view is still compiled and
linked; measured on the ADV target the anchor profile came out 140 bytes larger
than the full build, which is the table itself. A profile decides what a unit
does, not what it carries.

An app never mentions a profile, and a profile never edits an app.

## Seeing it without hardware

The simulator runs this same view, ui and store code against M5GFX's SDL panel
at the real 240x135, so a new app works there the day it is written. There is
nothing to register: the sim builds `src/views/` as it stands.

```bash
pio run -e sim -t exec            # a window you can drive
pio run -e sim-anchor -t exec     # the Anchor profile
```

As a test harness it writes a PPM before each scripted key, which is how a
screen gets checked without anybody watching a window:

```bash
.pio/build/sim-anchor/program --keys "aaa" --shot /tmp/frame --quit-after 9000
```

What is faked is the ring around the firmware: keys come from SDL, the battery
is a number, the IMU is the mouse, fetches answer from captures in
`sim/fixtures`, and the host link replays a capture in `sim/src/cable_sim.cpp`.
Every capture is real bytes off the real source, because a fixture that was
typed by hand agrees with whatever the code happens to do, which is the one
thing a fixture must never do.

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
- **`cable.*` exists at all.** An app whose data comes from a desktop had
  nowhere to read from, and the answer was a seam beside `net`, with a
  simulator half, rather than a view holding a serial port open.
- **Profiles exist at all.** A unit meant to be one app had no way to be one
  without deleting the other ten.
