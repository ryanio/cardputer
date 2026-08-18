# cardputer

Coral app for the M5Stack Cardputer ADV. Three views over
[coral](https://0xcoral.com), [glyphbots](https://www.glyphbots.com) and
[gwei](https://gwei.ryanio.com).

| View | Source | Shows |
|------|--------|-------|
| Gas | gwei | Base fee, three speed tiers, 24h sparkline, threshold alarm |
| Bot | glyphbots | A GlyphBot from its Unicode genome, raised as a pet |
| Reef | coral | The daily Coral Score guessing round |

- [docs/API.md](docs/API.md) the three APIs, verified shapes, rate limits
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
cp include/secrets.h.example include/secrets.h   # WiFi SSID and password
pio run -t upload
pio device monitor
```

M5Cardputer 1.1.1 is the first release that drives the ADV's TCA8418 keyboard.
Older versions read no keys on this board.

## Stock firmware

The device ships with M5Stack's
[UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo) (`CardputerADV`
branch, MIT), which is a useful HAL reference and is on the `upstream` remote.
Flashing ours replaces it; M5Burner puts it back. With three units, keeping one
stock costs nothing.

In that firmware, home is the G0 button on the top edge, not a key.
