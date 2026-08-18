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

See [CLAUDE.md](CLAUDE.md) for operating rules and [plans/ROADMAP.md](plans/ROADMAP.md)
for the build order.

## Hardware

M5Stack Cardputer (StampS3): ESP32-S3, 8MB flash, 8MB PSRAM, 240x135 ST7789
TFT, 56-key keyboard, speaker, microphone, IR transmitter, microSD, Grove port,
WiFi and BLE. Three units, which is what makes the multiplayer modes worth
building.

## Why this is a small project

All three sources are public, unauthenticated JSON over HTTPS. No API key ever
reaches the device, so there is nothing to leak in a flashed binary and nothing
to rotate. The device needs WiFi, a TLS GET and a JSON parse, and that is the
whole client.

All three hosts also present certificates from the same issuer, Google Trust
Services WE1, so a single bundled root CA covers every request the firmware
makes.

## The three sources

Verified live on 2026-08-17. Response shapes are trimmed to what the firmware
reads.

### gwei

```
GET https://gwei.ryanio.com/api/gas
{"baseFeeGwei":0.046545,
 "speeds":[{"speed":"fast","label":"Fast","eta":"~15 sec","totalGwei":0.110062,"usdPerTransfer":0.004412}, ...],
 "ethPriceUsd":1908.99,"blockNumber":25778316,"updatedAt":1787011995508}

GET https://gwei.ryanio.com/api/gas/history
{"points":[{"t":1786925698787,"gwei":0.03506,"tip":0.005396}, ...],"low24h":...,"high24h":...}
```

Exactly three speeds, always ordered fast, normal, cheap. `ethPriceUsd` is
`null` when no price could be fetched, never a placeholder, so the UI needs a
no-price state. History points are already thinned to one a minute, which is a
240px sparkline with no client-side downsampling.

The server refreshes its snapshot at most once per 30s and does it lazily on
request. Polling faster returns the same bytes.

### glyphbots

```
GET https://www.glyphbots.com/api/bot/1
{"bot":{"tokenId":1,"name":"Vector the Kind","rarityRank":1259,
  "unicode":{"textContent":["■▲▼▲▼▲","╱ ◎◎ ╱","╪","◈"],
             "colors":{"background":"hsl(98,20%,8%)","text":"hsl(278,85%,85%)"}},
  "traits":[{"trait_type":"Head","value":"▲▼▲▼▲"}, ...],
  "burnedAt":null,"burnedBy":null,
  "royalties":{"totalWei":"0","mintCount":0}}}
```

A GlyphBot is not an image. It is four short lines of Unicode plus a foreground
and background color, which is already a display format for a 240x135 screen.

The collection's full alphabet is 105 distinct non-ASCII glyphs across all
11,111 bots, counted from `GET /api/bots/facets`. Stock ESP32 fonts carry none
of them, so the firmware ships a generated bitmap atlas of those 105. See
`tools/glyph-atlas/` and the atlas rule in [CLAUDE.md](CLAUDE.md).

`royalties.mintCount` and `burnedAt` are the only per-bot fields that change
over time on this endpoint. They are what drives the pet.

### coral

```
GET https://api.0xcoral.com/api/v1/resolve?q=MEME
{"query":"MEME","resolved":{"address":"0xb131f4A5...","chain":"ethereum","symbol":"MEME"}}

GET https://api.0xcoral.com/api/v1/score/ethereum/0xb131f4A5...
{"score":61,"verdict":"unknown","confidence":0.649,"confidenceLabel":"medium",
 "explanation":{"headline":"Not enough corroborating signal for a confident read.",
   "bullets":["741305 holders · very deep base","top-10 hold 65% · moderately concentrated", ...],
   "caveats":["Not a price target, audit, or trading recommendation."]}}

GET https://api.0xcoral.com/api/v1/tokens/index?limit=3
GET https://api.0xcoral.com/api/v1/traction
GET https://api.0xcoral.com/api/v1/dashboard
GET https://api.0xcoral.com/api/v1/og/token/{chain}/{address}   -> 302 to a rendered card
```

`resolve` is what makes a 56-key thumb keyboard usable here. Typing a 42
character contract address is miserable; typing `MEME` is not.

`explanation.bullets` come back as five short lines already sized for a narrow
column. The `caveats` array is not decoration, and the API sets an
`x-coral-attribution` header stating the same thing. Both get screen space.

Score is a heavy full lookup that self-rate-limits and took several seconds when
probed. It is request-response with a spinner, never a poll, and the game
prefetches rather than blocking a round on it.

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
plans/
  ROADMAP.md
```
