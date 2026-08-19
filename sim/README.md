# The simulator

The firmware's own `view`, `ui`, `store` and view code on a desktop, against
M5GFX's SDL panel as a Cardputer ADV. The graphics library, the fonts and the
240x135 geometry are the real ones, so what you see is what the panel draws.

```bash
brew install sdl2
pio run -e sim -t exec           # a window you can drive
pio run -e sim -t exec -a --tour # it drives itself and says what it is pressing
```

Keys map to the device: the four keys that print arrows are the arrows, Escape
is the backtick, Alt is Fn, Home or F1 is the G0 button. Settings persist in
`sim-nvs-flint.txt` next to the binary.

The mouse is the IMU. Where the pointer sits in the window is how far the unit
is tipped, holding the left button rattles it, and F2 turns it face down, which
is what puts the panel to sleep. Maze, Rain and the dial under a Coral guess
all read that, so a motion view is something anybody can try without a unit.

## As a test harness

One PPM frame before each scripted key, which is how a view gets checked
without anyone looking at a window. `\n` is enter, `\b` is backspace.

```bash
.pio/build/sim/program --keys "5\n\n.\n" --shot /tmp/frame --quit-after 11000
.pio/build/sim/program --latency 4000 --keys "\n" --shot /tmp/frame
.pio/build/sim/program --tilt 0.5,0 --keys "1\n" --shot /tmp/frame
.pio/build/sim/program --orbit 5 --shake --keys "9\n" --shot /tmp/frame
```

`--tilt x,y` pins the lean where a screenshot wants it, right and down
positive and 1 the whole way over. `--orbit 5` rolls the lean all the way round
every five seconds, which keeps anything that falls or rolls moving on its own,
and `--shake` rattles the unit. Together they are how a motion view gets
captured by a script rather than by somebody waving a mouse.

`--latency` holds every fetch, since a fixture answers instantly and would
otherwise hide the screen a view draws while it waits. Four seconds is roughly
what a Coral score costs on a unit.

## What is real and what is not

Real: everything above the driver layer. Layout, navigation, the exit
convention, the row grid, NVS behaviour, every pixel the panel draws.

Simulated: the keyboard comes from SDL, the battery is a number, the IMU is
the mouse, and the network answers from `net_sim.cpp` with a fixed list of access points. A
passphrase under eight characters fails, so the join failure path has something
to fail on.

## Fetches

Fetches answer from `sim/fixtures`, real captures rather than invented bodies.
`tools/apicheck/check.py --save` refreshes them from the live sources and
`tools/fixtures/bundle.py` compiles them into `sim/src/fixtures.h`, because the
web build has no filesystem. `manifest.json` says which URL each one answers.

An unmatched URL fails with "no fixture" rather than inventing a body, and
every token is routed by its own address, so a view shows its own numbers or
says it has none. Coral rate limits scores hard enough that only a few tokens
have one captured, which is why guessing a ticker here reaches the reveal and
then says Coral had no answer.

One fixture is a real 148KB JPEG, because a decoder cannot be checked against a
description of a photo. It is also the one place the simulator is not the
firmware: M5GFX only offers streaming `drawJpg(Stream*)` when it thinks it is
on Arduino, and telling it that on a desktop swaps the whole panel driver, so
`sim/src/jpeg_sim.cpp` reads the body first. The fit, the crop, the caption and
the browsing are still checked here; whether the stream decodes inside 320KB
beside a TLS session is unknown 3 in [../docs/PLAN.md](../docs/PLAN.md).
