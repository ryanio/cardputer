# flint

Firmware for the M5Stack Cardputer ADV. Eight apps: a drum machine, a breathing
coach, and six views over
[coral](https://0xcoral.com), [bankr](https://bankr.bot),
[glyphbots](https://www.glyphbots.com), [voxels](https://www.voxels.com) and
[gwei](https://gwei.ryanio.com).

**[ryanio.github.io/cardputer](https://ryanio.github.io/cardputer/)** has the
screenshots and how to run it without hardware.

| View | Source | Shows |
|------|--------|-------|
| Reef | coral | The daily Coral Score guessing round |
| Bankr | bankr | Agents by market cap and weekly revenue, scored through Coral |
| Bot | glyphbots | A GlyphBot from its Unicode genome, raised as a pet |
| Womp | voxels | The latest in-world photo, as a desk frame |
| Gas | gwei | Base fee, three speed tiers, 24h sparkline, threshold alarm |

- [docs/API.md](docs/API.md) the five APIs, verified shapes, rate limits
- [sim/README.md](sim/README.md) the simulator, and how it takes screenshots
- [docs/ROADMAP.md](docs/ROADMAP.md) build order
- [CLAUDE.md](CLAUDE.md) rules

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
pio run -t upload
pio device monitor
```

WiFi is typed on the device, under Setup in the menu, and kept in NVS. That is
what makes a unit usable by whoever you give it to. For a dev unit that joins
on first boot, `cp include/secrets.h.example include/secrets.h` and fill it in;
anything typed on the device wins over it.

M5Cardputer 1.1.1 is the first release that drives the ADV's TCA8418 keyboard.
Older versions read no keys on this board.

## Layout

| Path | Holds |
|------|-------|
| `src/main.cpp` | boot report, the loop |
| `src/net.*` | WiFi, HTTPS with the bundled roots, JSON and streaming fetches |
| `src/ui.*` | the 240x135 layout, colors, status bar |
| `src/view.*` | view registry, menu, input, the exit convention |
| `src/store.*` | NVS settings |
| `src/views/*.cpp` | one file per view, each registering itself |
| `src/ca_roots.h` | the two root CAs every host chains to |

The four spine headers are frozen. A view adds itself with `VIEW_REGISTER`
and never edits them.

## Stock firmware

The device ships with M5Stack's
[UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo) (`CardputerADV`
branch, MIT), which is a useful HAL reference and is on the `upstream` remote.
Flashing ours replaces it; M5Burner puts it back. With three units, keeping one
stock costs nothing.

In that firmware, home is the G0 button on the top edge, not a key.
