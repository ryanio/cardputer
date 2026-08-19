# Engineering plan

What is built, and what the first unit answered.

## The constraint

**A screen still needs a person in front of it.** An agent on Ryan's machine
can flash a unit and read the port back with `tools/serial/read.py`, so
anything the firmware prints is checkable without him. Anything it draws, plays
or is tipped is not: nobody in the build loop can look at the panel, hear the
speaker, or pick the unit up.

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

## Settled on a unit, 2026-08-18

The first flash happened. The report, read back over serial:

```
flint 0.1.0, built Aug 18 2026 18:34:45
chip ESP32-S3 rev 0, 2 cores at 240 MHz
flash 8192 KB, sketch 1191 KB of 4455 KB
psram none
heap 300 KB free of 336 KB, largest block 271 KB
net: online as 192.168.4.111, rssi -48 dBm
net: clock set, 2026 08 18 22:01 ET
probe: tls ok, heap 247 KB before, 246 KB after, 186 KB low water
probe: base fee 0.0495 gwei
```

**Unknown 1, PSRAM: none.** The streaming decode stays the only way a photo
reaches the panel.

**Unknown 2, does TLS fit: yes, with room.** 300KB free at boot against the
336KB the chip actually reports, and a handshake plus a fetch bottoms out at
186KB. A live session costs around 60KB while it is open.

**Unknown 3, does the streaming decode hold: yes, and it costs nothing.** A
148,390 byte photo landed on the panel in 2649ms, over the same TLS session
that fetched its metadata:

```
net: ok .../womps/81300.json 760 B in 1782 ms, heap 248872 free, 190896 low
net: ok .../womp_1786937202867_....jpg 148390 B in 2649 ms, heap 249816 free, 185368 low
womp: 81300, 144KB in 2649ms
```

The low water mark under a 144KB image is 185KB, against 186KB for the boot
probe that fetches 535 bytes. The image never exists in RAM, so its size does
not show up in the heap at all. Free heap after is higher than before, so the
first one did not leak.

Settled in passing: the TCA8418 reads keys, a network typed on the unit joins
out of NVS, SNTP lands, and gwei answers over the bundled roots rather than
over a desktop's trust store.

This was read by pulsing RTS on the port and listening, because PlatformIO's
own monitor wants a terminal. `tools/serial/read.py` is that, if it helps.

**Unknown 4, does the speaker work: yes.** Ryan heard Beat keep time on the
unit. The gas alarm and Calm's gong go through the same `Speaker.tone` and
`playRaw`, so all three are covered by that.

## Unknowns still open

5. **Which way up is the IMU?** M5Unified fixes axes per board and has no case
   for a Cardputer, so the four constants at the top of `src/motion.cpp` are a
   guess until a unit prints a sample.

## What is left

- **The rest of the flash gate.** Only the IMU axes are left: tip the unit in
  Maze or Rain and see whether the marble rolls the way it is leaning.
- **OTA**, Phase 2, including image signing. The only piece of the original
  plan still unwritten.
- **Phases 5, 7 and 8**: the pet, three units talking, off-device work. Those
  wait for hardware feedback.
