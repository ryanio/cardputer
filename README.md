# cardputer

A Coral app for the M5Stack Cardputer ADV, showing data from
[coral](https://0xcoral.com), [glyphbots](https://www.glyphbots.com) and
[gwei](https://gwei.ryanio.com).

Not a firmware from scratch. This is a fork of M5Stack's own
[M5Cardputer-UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo)
(MIT, `CardputerADV` branch) with one app added to its launcher. That app has
its own sub-menu:

| View | Source | What it does |
|------|--------|--------------|
| Gas | gwei | Live Ethereum base fee, three speed tiers, 24h sparkline, threshold alarm |
| Bot | glyphbots | A GlyphBot rendered from its Unicode genome, raised as a pet |
| Reef | coral | The daily Coral Score guessing round |

Forking buys the launcher, the keyboard driver, the display stack, the audio
codec, WiFi provisioning and the home-button convention. All three sources are
public, unauthenticated JSON over HTTPS, so no API key ever reaches the device.

## Docs

- [docs/API.md](docs/API.md) covers the three sources, with verified response
  shapes and the rate limits worth respecting.
- [docs/ROADMAP.md](docs/ROADMAP.md) sequences the build.
- [CLAUDE.md](CLAUDE.md) holds the operating rules. `AGENTS.md` symlinks to it.

## Hardware

M5Stack **Cardputer ADV** (SKU K132-Adv), three units.

- ESP32-S3FN8, Xtensa LX7 dual core at 240MHz, 8MB flash
- 1.14" ST7789V2, 240x135
- 56-key keyboard (4 x 14) on a TCA8418 controller
- BMI270 6-axis IMU
- ES8311 audio codec, NS4150B amp, 8R 1W speaker, 3.5mm jack
- MEMS microphone, 65dB SNR
- IR emitter, microSD, Grove HY2.0-4P, EXT 2.54-14P
- 1750mAh battery
- WiFi and BLE

Not the original Cardputer v1.1. The ADV has the IMU, the better audio path and
the bigger battery, and it runs a different firmware branch. GPS and LoRa appear
as apps in the stock firmware but are **not** onboard; those drive Grove and EXT
modules.

**Home is the G0 button on the top edge**, not a key. Every app polls it and
closes. Ours must too.

## Stack

ESP-IDF **v5.4.2**. No Arduino, no PlatformIO. JSON is cJSON and TLS is esp-tls
with the built-in certificate bundle, both already in ESP-IDF, so there are no
third-party libraries to add.

## Setup

```bash
git clone https://github.com/m5stack/M5Cardputer-UserDemo.git
cd M5Cardputer-UserDemo
git checkout CardputerADV
python3 ./fetch_repos.py
idf.py build
```

Flash with the power switch off, holding G0, then powering on from the rear to
enter download mode:

```bash
idf.py flash monitor
```

## Adding the app

Three edits, following `app_dummy` as the template:

1. Copy `main/apps/app_dummy/` to `main/apps/app_coral/` and rename the class.
2. Add its header to `main/apps/apps.h`.
3. Register it in `main/main.cpp`:
   `GetMooncake().installApp(std::make_unique<AppCoral>());`

Menu order follows registration order in `main.cpp`.
