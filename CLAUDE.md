# flint

Firmware for the M5Stack Cardputer ADV. Arduino under PlatformIO. Coral is one
of the sources it reads, not the name of the thing.
`AGENTS.md` is a symlink to this file.

- [README.md](README.md) hardware, build, layout
- [docs/API.md](docs/API.md) the five sources, verified shapes, rate limits, CA roots
- [docs/ROADMAP.md](docs/ROADMAP.md) phases
- [docs/PLAN.md](docs/PLAN.md) what is built, and what the first unit answered

## Build

```bash
pio run                 # compile
pio run -t upload       # flash
pio device monitor      # serial
tools/fmt.sh            # house style, --check enforces it
tools/apicheck/check.py # ask the five sources whether they still fit
```

Green as of 2026-08-18: RAM 18.4%, flash 37.0% of the 3.3MB app slot. CI
compiles both targets on a push and probes the sources on a schedule.

## Traps

- **M5Cardputer 1.1.1+.** The ADV keyboard is a TCA8418; the original was a
  74HC138. Older versions compile fine and read no keys.
- **Flashing is scriptable, looking is not.** `pio run -t upload` and
  `tools/serial/read.py` work from an agent, so anything printed is checkable.
  Anything drawn, played or tipped needs Ryan. "Compiles" is never "works".
- **No secrets on the device.** All five sources are public. If something seems
  to need a key, the design is wrong. WiFi is typed on the unit and kept in
  NVS; gitignored `include/secrets.h` is a dev-unit fallback only.
- **Never enable USB HID.** These units get given away. Serial only.
- **Give every view an exit.**
- **The menu is a carousel and it draws between repaints.** It is the only
  screen that does. The 42KB sprite behind the slide is held while it moves and
  handed back a second after it stops, because the boot probe opens a TLS
  session while the menu is on screen.
- **Which way up the IMU is glued has never been checked.** M5Unified fixes
  axes per board and has no case for a Cardputer, so the answer is four
  constants at the top of `src/motion.cpp` and a raw sample in the boot report.
  Read motion through `motion::`, never `M5.Imu` directly, and a wrong sign
  stays one edit rather than one per view.
- **Poll windows.** gwei refreshes at most every 30s. Coral's `/guess/daily` is
  one round per ET day: fetch once, play offline. Never loop `/score`.
- **Three headers are generated, never hand edited**: `src/icons.h`,
  `src/glyphs.h`, `sim/src/fixtures.h`. Each has `--check` and CI runs it.
- **Sample more than one of anything.** Bot 1 is the exception, not the shape.
- **A GlyphBot is text, not an image.** Render `unicode.textContent` with
  `unicode.colors`, which come as `#rrggbb` or `hsl()`. Never fetch a PNG.
- **Womp JPEGs are 45KB to 152KB against 320KB of RAM with TLS inside it.**
  Decode from the stream through `src/jpeg.cpp`, never fetch-then-decode.
- **Anything showing a Coral score shows its caveats and the Coral name.**
  `src/coral.*` is the only place that draws one.
- **The simulator answers from real captures**, routed per URL in
  `sim/fixtures/manifest.json`. An unmatched URL fails rather than serving a
  different token's numbers.
- **The status bar names the source of the current view** (GLYPHBOTS, GWEI,
  CORAL, VOXELS), not the app. It costs 10 rows. A full screen view has no bar
  and paints the source name into its own corner.

## Screen

240x135. Four lines of large text, or about eight small. Design for eight.
56 keys and no sane way to type 42 hex characters, so anything needing an
address goes through Coral's `/resolve` with a ticker.

## Commits

Commit at checkpoints, not only when asked. Scope each to its own ask. Straight
to `main`, no branches. No dashes in code, comments or commit messages.
