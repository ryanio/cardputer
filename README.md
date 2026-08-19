# flint

Firmware for the M5Stack Cardputer ADV. Ten apps: a drum machine, a breathing
coach, a marble maze and a glyph downpour you steer by tipping the unit, and
six views over
[coral](https://0xcoral.com), [bankr](https://bankr.bot),
[glyphbots](https://www.glyphbots.com), [voxels](https://www.voxels.com) and
[gwei](https://gwei.ryanio.com).

**[ryanio.github.io/cardputer](https://ryanio.github.io/cardputer/)** has the
screenshots and how to run it without hardware.

| View | Source | Shows |
|------|--------|-------|
| Reef | coral | Guess a Coral Score: a ticker, a random token, or the one everybody gets today |
| Bankr | bankr | Agents by market cap and weekly revenue, scored through Coral |
| Bot | glyphbots | A GlyphBot drawn from its Unicode, the sheet behind it, and four ways to browse |
| Womp | voxels | In-world photos, newest first, decoded straight to the panel |
| Gas | gwei | The tip you pick, then the day it sits in: both series over 24h, and hour by hour |

- [docs/API.md](docs/API.md) the five APIs, verified shapes, rate limits
- [sim/README.md](sim/README.md) the simulator, and how it takes screenshots
- [docs/PLAN.md](docs/PLAN.md) what is built, and what the first unit answered
- [docs/ROADMAP.md](docs/ROADMAP.md) build order
- [CLAUDE.md](CLAUDE.md) rules

The first unit ran on 2026-08-18: no PSRAM, TLS fits with 186KB to spare, and
a 144KB photo decodes to the panel in 2.6 seconds without ever existing in RAM.
The speaker and the IMU axes are still unheard and unchecked. Most screenshots
came out of the simulator, which is why the simulator exists.

## Hardware

Cardputer ADV (K132-ADV), three units. ESP32-S3FN8 (Stamp-S3A), 8MB flash.
240x135 ST7789V2. 56 keys via TCA8418. BMI270 IMU. ES8311 codec, 1W speaker,
3.5mm jack. MEMS mic. IR emitter. microSD. Grove HY2.0-4P + EXT 2.54-14P.
1750mAh. Magnetic back, LEGO holes.

Not the original v1.1: different keyboard controller (TCA8418 vs 74HC138),
different audio (ES8311 vs NS4168+SPM1423), plus the IMU and the 3.5mm jack.
GPS and LoRa are Grove/EXT modules, not onboard.

## Build

```bash
pio run -t upload             # flash, then pio device monitor
pio run -e sim -t exec        # the simulator, which is how a view gets looked at
tools/fmt.sh                  # house style, --check enforces it
tools/apicheck/check.py       # ask the five sources whether they still fit
```

Everything but the first line runs without a device.

WiFi is typed on the device under Setup and kept in NVS, so a unit works for
whoever holds it. For a dev unit that joins on first boot,
`cp include/secrets.h.example include/secrets.h`; anything typed on the device
wins over it.

M5Cardputer 1.1.1 is the first release that drives the ADV's TCA8418 keyboard.
Older versions read no keys.

## Layout

| Path | Holds |
|------|-------|
| `src/main.cpp` | boot report, the loop |
| `src/net.*` | WiFi, HTTPS with the bundled roots, JSON and streaming fetches |
| `src/ui.*` | the 240x135 layout, colors, status bar |
| `src/view.*` | view registry, menu, input, the exit convention |
| `src/store.*` | NVS settings |
| `src/motion.*` | the IMU, filtered once: tilt, shake, face down |
| `src/rest.*` | face down sleeps the panel, a key or turning it over wakes it |
| `src/views/*.cpp` | one file per view, each registering itself |
| `src/coral.*` | a Coral score and the two screens it is shown through |
| `src/jpeg.*` | a photo decoded off the socket, split per target like net |
| `src/glyphs.h` | generated: the 105 characters a GlyphBot is drawn from |
| `tools/icons/` | builds the menu icon atlas from Lucide |
| `src/ca_roots.h` | the root CAs every host chains to |
| `sim/` | the simulator, and the captured fixtures its network answers from |
| `tools/apicheck/` | the contract check over all five sources |
| `tools/fixtures/` | compiles the captures into a header the simulator links |
| `tools/glyphs/` | builds the atlas from the collection's own alphabet |

The four spine headers are frozen. A view adds itself with `VIEW_REGISTER`
and never edits them.

## Stock firmware

The device ships with M5Stack's
[UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo) (`CardputerADV`
branch, MIT), a useful HAL reference, on the `upstream` remote. Flashing ours
replaces it and M5Burner puts it back, so with three units keeping one stock
costs nothing. In that firmware home is the G0 button, not a key.
