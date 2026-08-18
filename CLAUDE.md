# flint

Firmware for the M5Stack Cardputer ADV. Arduino under PlatformIO. Coral is one
of the sources it reads, not the name of the thing.
`AGENTS.md` is a symlink to this file.

- [README.md](README.md) hardware, build, layout
- [docs/API.md](docs/API.md) the four APIs, verified shapes, rate limits, CA roots
- [docs/ROADMAP.md](docs/ROADMAP.md) phases
- [docs/PLAN.md](docs/PLAN.md) how the work is split, and the flash gates

## Build

```bash
pio run                 # compile
pio run -t upload       # flash
pio device monitor      # serial
```

Green as of 2026-08-18: RAM 16.7%, flash 35.4% of the 3.3MB app slot.

`tools/fmt.sh` applies the house style and `--check` enforces it.
`tools/apicheck/check.py` asks all five sources whether they still answer the
way the firmware reads, and `--save` refreshes the simulator's fixtures.
Both run in CI, the second on a schedule.

## Traps

- **M5Cardputer 1.1.1+.** The ADV keyboard is a TCA8418; the original was a
  74HC138. Older versions compile fine and read no keys.
- **Nobody in the build loop can flash.** Agents compile. Only Ryan verifies.
  "Compiles" is never "works", and a phase is not done until he has looked.
- **No secrets on the device.** All four APIs are public and unauthenticated. If
  something seems to need a key, the design is wrong. WiFi is typed on the unit
  and kept in NVS. Gitignored `include/secrets.h` is a fallback for a dev unit
  only, and a unit you hand to someone else has none compiled in.
- **Never enable USB HID.** The ESP32-S3 can present as a keyboard. These units
  get given away and may come back. Serial only.
- **Give every view an exit.** A screen you cannot leave is the bug that annoys
  you every session.
- **Poll windows.** gwei refreshes at most every 30s. Coral's `/guess/daily` is
  one round per ET day: fetch once, play offline. Never loop `/score`; three
  devices hammering it get all three blocked.
- **The glyph atlas is generated, never hand-edited.** Regenerate and commit both.
- **A GlyphBot is text, not an image.** Render `unicode.textContent` with
  `unicode.colors`. Never fetch a PNG for a bot.
- **Womp JPEGs are ~128KB and RAM is 320KB with TLS inside it.** Stream the
  decode to the panel, scaling as you go. Never fetch-then-decode.
- **Anything showing a Coral score shows its caveats and the Coral name.**
  `src/coral.*` is the one place that draws one, so the rule has one
  implementation rather than one per view.
- **The status bar names the source of the current view** (GLYPHBOTS, GWEI,
  CORAL, VOXELS), not the app. It costs 10 rows, and a view that skips the
  title gets the other eight lines. A full screen view has no bar and paints
  the source name over its own corner instead.

## Screen

240x135. Four lines of large text, or about eight small. Design for eight.
56 keys and no sane way to type 42 hex characters, so anything needing an
address goes through Coral's `/resolve` with a ticker.

## Commits

Commit at checkpoints, not only when asked. Scope each to its own ask. Straight
to `main`, no branches. No dashes in code, comments or commit messages.
