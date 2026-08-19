# Engineering plan

What is built, and the flash gate that has not been run.

## The constraint

**Nobody in the build loop can flash.** Agents and the manager compile; only
Ryan can put a binary on a device and look at it. Every view is now written and
none of it has ever run on hardware.

## What got built

The spine landed as planned. The three-agent fan-out behind frozen headers did
not happen: the views were built one at a time, each driven and screenshotted
in the simulator before the next started. Four of the five sources contradicted
their documentation somewhere, and every one of those was found by looking at a
real payload.

```
src/net.{h,cpp}      WiFi join, HTTPS GET with the CA bundle, JSON and streams
src/ui.{h,cpp}       the 240x135 layout, colors, text, the atlas
src/view.{h,cpp}     view registry, menu, input loop, the exit convention
src/store.{h,cpp}    NVS settings
src/coral.{h,cpp}    a score and the two screens it is shown through
src/jpeg.{h,cpp}     a photo decoded off the socket, split per target
src/glyphs.h         generated: the 105 characters a GlyphBot is drawn from
src/views/*.cpp      eight views, none of them a stub
```

Three things not in the original plan, all because a desktop cannot flash and
so has to check everything else: `tools/apicheck/check.py` probes all five
sources over the device's own CA roots, `sim/fixtures` answers the simulator
from real captures, and `tools/glyphs/generate.py` builds the atlas from the
collection's own alphabet.

## The flash gate

Ten minutes, and it settles every open unknown.

```bash
pio run -t upload
pio device monitor
```

**1. Boot report.** Four lines answer the oldest questions here:

```
psram none                                       or psram yes, N KB
imu yes, accel 0.00 0.00 1.00 g                  or imu none
heap N KB free of N KB, largest block N KB
probe: tls ok, heap N KB before, N KB after, N KB low water
probe: base fee 0.0983 gwei
```

`psram none` is expected and settles unknown 1. The low water mark settles
unknown 2. The accel line settles unknown 5, and it has to be read with the
unit flat on a desk and the screen up: Z near +1, X and Y near 0. Then tip the
right hand edge down, which should push X positive, and tip the top edge away,
which should push Y positive. Whichever is not is a sign to flip in
`src/motion.cpp`, and nothing else in the firmware has to change.

**2. Setup.** Scan, pick, type, join, power cycle. No keys read at all means an
M5Cardputer older than 1.1.1 got linked.

**3. Menu.** Ten cards over two screens, every one opens and exits on the
backtick. A screen
that traps you is a spine bug, since the loop enforces the exit, not the view.

**4. Gas.** Live fee, three tiers, a sparkline with a shape in it. Then arm the
alarm above the current fee: nobody has heard the arpeggio. Leave it a minute
to watch it poll at 30s.

**5. Womp.** The one that can fail on memory. A photo should fill the panel in
a few seconds; a blank screen and an allocation message settles unknown 3. Walk
one id down to confirm the first did not leak.

**6. Bot and Reef.** A bot should look like the site's version of the same bot,
in its own two colors. Play one Reef round to the reveal, and lean the unit to
pick the number rather than typing it.

**7. Maze, Rain, and face down.** Both are the axis check made visible: a
marble that rolls the way the unit is tipped and glyphs that pour downhill mean
the constants are right. Shake the maze title screen for the maze that is not
in the count. Then put the unit face down for three seconds, which should black
the panel, and turn it back over, which should bring it straight back.

All of the above is verified in the simulator, so anything that differs is a
driver, a memory or a TLS difference.

## Unknowns only hardware can settle

1. **Is there PSRAM?** M5 does not list any for the Stamp-S3A.
2. **Does TLS fit** beside the display buffer in 320KB? Every network view
   depends on yes.
3. **Does the streaming decode hold** at 45KB to 152KB an image? It holds a
   3.9KB work pool and nothing else by construction.
4. **Does the speaker work?** Beat, Calm and the gas alarm all call
   `Speaker.tone`, and the simulator's speaker is a no-op.
5. **Which way up is the IMU?** M5Unified fixes axes per board and has no case
   for a Cardputer, so the four constants at the top of `src/motion.cpp` are a
   guess until a unit prints a sample.

## What is left

- **The flash gate.** Nothing else should be built first.
- **OTA**, Phase 2, including image signing. The only piece of the original
  plan still unwritten.
- **Sound in the browser build.** `sim/include/M5Cardputer.h` has a no-op
  Speaker, so the site demo is silent.
- **Phases 5, 7 and 8**: the pet, three units talking, off-device work. Those
  wait for hardware feedback.
