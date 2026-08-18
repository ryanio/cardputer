# The simulator

The firmware's own `view`, `ui`, `store` and view code, running on a desktop
against M5GFX's SDL panel as a Cardputer ADV. The graphics library, the fonts
and the 240x135 geometry are the real ones, so what you see is what the panel
draws.

```bash
brew install sdl2
pio run -e sim -t exec           # a window you can drive
pio run -e sim -t exec -a --tour # it drives itself and says what it is pressing
```

Keys map to the device: the four keys that print arrows are the arrows, Escape
is the backtick so it always backs out, Alt is Fn, and Home or F1 is the G0
button. Settings persist in `sim-nvs-coral.txt` next to the binary, the way NVS
persists across a reboot.

## As a test harness

Frames come out as PPM, one before each scripted key, which is how a change to
a view gets checked without anyone looking at a window.

```bash
.pio/build/sim/program --keys "5\n\n.\n" --shot /tmp/frame --quit-after 11000
```

`\n` is enter and `\b` is backspace. This found two real bugs the first time it
ran: pressing enter during a WiFi scan killed the scan and left an empty list
forever, and a device with nothing saved claimed its network was built in.

## What is real and what is not

Real: everything above the driver layer. Layout, navigation, the exit
convention, the row grid, NVS behaviour, and every pixel the panel draws.

Simulated, in `sim/`: the keyboard comes from SDL, the battery is a number, and
the network answers from `net_sim.cpp` with a fixed list of fake access points.
A passphrase under eight characters fails, so the join failure path has
something to fail on.

## Fetches

A fetch answers from `sim/fixtures`, which are real captures rather than
invented bodies: `tools/apicheck/check.py --save` refreshes them from the live
sources and `tools/fixtures/bundle.py` compiles them into `sim/src/fixtures.h`,
because the web build has no filesystem to read them from.
`sim/fixtures/manifest.json` says which URL each one answers.

A URL nothing matches fails with "no fixture" rather than inventing a body.
That is deliberate twice over: a view's error state gets exercised, and a demo
never shows one token's score under another token's name. Every token is routed
by its own address for the same reason, so a round shows its own numbers or
says it has none. Coral's score endpoint rate limits hard enough that only a
few tokens have a captured score, which is why guessing a ticker in the
simulator reaches the reveal and then says Coral had no answer.

One fixture is a real 148KB JPEG rather than JSON, because the womp view
decodes a photo off the socket and there is no way to check that against a
description of one. That is also the one place the simulator is not the
firmware: M5GFX only offers its streaming `drawJpg(Stream*)` when it thinks it
is on Arduino, and telling it that on a desktop swaps the whole panel driver,
so `sim/src/jpeg_sim.cpp` reads the body first and hands over a buffer. What
the desktop still checks is the fit, the crop, the caption and the browsing.
Whether the stream decodes inside 320KB beside a TLS session is unknown 3 in
docs/PLAN.md, and only a device answers it.

Fixtures answer instantly, which hides the screen a view draws while it waits.
`--latency 4000` holds every fetch for four seconds, which is roughly what a
Coral score costs on a real unit.

```bash
.pio/build/sim/program --latency 4000 --keys "\n" --shot /tmp/frame
```
