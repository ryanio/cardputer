# cardputer

A Coral app for the M5Stack Cardputer ADV, Arduino under PlatformIO.
`AGENTS.md` is a symlink to this file.

Hardware and setup live in [README.md](README.md). Endpoint shapes and rate
limits live in [docs/API.md](docs/API.md). Build order lives in
[docs/ROADMAP.md](docs/ROADMAP.md). Read them before editing.

## Operating Contract

1. **Think before coding.** Read the closest doc first. State assumptions when
   they matter.
2. **Simplicity first.** Smallest change that solves the request. One screen,
   one app, three views.
3. **Surgical changes.** Every changed line traces to the ask.
4. **Verify on hardware.** A change that compiles is not a change that works.
   Flash it and look at the screen before calling it done.

## Don't re-derive these

- **M5Cardputer 1.1.1 or newer, always.** The ADV drives its keyboard through a
  TCA8418 controller; the original Cardputer used a 74HC138. Older library
  versions compile fine and then read no keys at all.
- **Never enable USB HID.** The ESP32-S3's native USB can present as a keyboard,
  and M5's stock firmware ships an app that does exactly that. A device which can
  type into a host the moment it is plugged in is the BadUSB class, and these
  units are being given away and may come back. Our firmware enumerates as CDC
  serial only. If a feature ever seems to need HID, that is a conversation, not a
  commit.
- **Give every view a way out.** The stock firmware trains users that G0 on the
  top edge is home. Match it, and never leave a screen with no exit.
- **No secrets ever reach the device.** All three APIs are public and
  unauthenticated. If a task seems to need a key, the design is wrong. WiFi
  credentials come from the stock `app_set_wifi` provisioning, so we do not
  handle them at all.
- **Respect the upstream cache windows.** gwei refreshes at most once per 30s,
  so poll no faster. Coral's `/api/v1/guess/daily` is one precomputed round per
  ET day: fetch it once and play offline. Never poll `/api/v1/score` in a loop;
  it is a heavy lookup with its own stricter limiter, and three devices hammering
  it get all three blocked.
- **The glyph atlas is generated, never hand-edited.** The tooling reads the
  live alphabet from `GET https://www.glyphbots.com/api/bots/facets` and emits
  the header. Editing the header directly means the next regeneration silently
  drops the edit. Regenerate and commit both.
- **Coral output carries its caveats.** The round's `answer.explanation.caveats`
  and the Coral name appear on any screen showing a score. A display contract,
  not a nicety.
- **A GlyphBot is text, not an image.** Render from `unicode.textContent` and
  `unicode.colors`. Never fetch or decode a PNG for a bot.

## Hardware constraints worth remembering

- 240x135. Four lines of large text, or roughly eight small. Design for the
  smaller number.
- `hsl(...)` strings come from the API and the display wants RGB565. Convert in
  one place, not at each call site.
- 56 keys and no comfortable way to type 42 hex characters. Any flow needing a
  contract address goes through coral's `/api/v1/resolve` with a ticker.
- ESP-NOW and WiFi share one radio and one channel. A peered unit and a fetching
  unit want different things. Pick one per app state.
- The ADV has a BMI270 IMU the original lacks, so tilt and shake are available
  as input.

## Verification

```bash
pio run                 # compile
pio run -t upload       # flash the connected unit
pio device monitor      # serial log
```

Docs-only changes need neither. Anything touching rendering, timing or the
network path gets flashed and looked at.

## Commits

Commit at meaningful checkpoints, not only when asked. Scope each commit to its
own ask. Commit onto `main` directly, no branches.

