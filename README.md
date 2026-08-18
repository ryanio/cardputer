# cardputer

Firmware for three M5Stack Cardputers that display and play with data from
[coral](https://0xcoral.com), [glyphbots](https://www.glyphbots.com) and
[gwei](https://gwei.ryanio.com).

Three apps in one firmware, picked from an on-device launcher:

| App | Source | What it does |
|-----|--------|--------------|
| `gas` | gwei | Live Ethereum base fee, three speed tiers, 24h sparkline, threshold alarm |
| `bot` | glyphbots | A GlyphBot rendered from its Unicode genome, raised as a pet |
| `reef` | coral | Guess-the-Coral-Score game, single player or head to head |

All three sources are public, unauthenticated JSON over HTTPS, so no API key
ever reaches the device.

## Docs

- [docs/API.md](docs/API.md) covers the three sources, with verified response
  shapes and the rate limits worth respecting.
- [docs/ROADMAP.md](docs/ROADMAP.md) sequences the build, from plumbing through
  multiplayer.
- [CLAUDE.md](CLAUDE.md) holds the operating rules. `AGENTS.md` symlinks to it.

## Hardware

M5Stack Cardputer (StampS3): ESP32-S3, 8MB flash, 8MB PSRAM, 240x135 ST7789
TFT, 56-key keyboard, speaker, microphone, IR transmitter, microSD, Grove port,
WiFi and BLE. Three units, which is what makes the multiplayer modes worth
building.

## Stack

PlatformIO, Arduino framework, board `m5stack-stamps3`. Libraries: `M5Cardputer`
(which pulls M5Unified and M5GFX) and `ArduinoJson`.

## Setup

```bash
cp include/secrets.h.example include/secrets.h   # WiFi SSID and password
pio run -t upload
pio device monitor
```

`include/secrets.h` is gitignored. WiFi credentials are the only local config,
and once the on-device setup screen lands they move to NVS and the file goes
away.

## Layout

```
src/
  main.cpp            boot, WiFi, launcher
  apps/gas/           gwei app
  apps/bot/           glyphbots app
  apps/reef/          coral app
lib/
  net/                TLS client, root CA, fetch-and-parse helpers
  ui/                 screen primitives, fonts, colors, sparkline
  store/              NVS and SD persistence
  link/               ESP-NOW peering between units
tools/
  glyph-atlas/        generates the 105-glyph bitmap header
include/
  secrets.h.example
```
