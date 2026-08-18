# The three sources

Every endpoint below was probed live on 2026-08-17. Response bodies are trimmed
to the fields the firmware reads.

All three are public, unauthenticated JSON over HTTPS. No API key ever reaches
the device, so there is nothing to leak in a flashed binary and nothing to
rotate.

All three hosts also present certificates from the same issuer, Google Trust
Services WE1, so a single bundled root CA covers every request the firmware
makes.

## gwei

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

## glyphbots

```
GET https://www.glyphbots.com/api/bot/1
{"bot":{"tokenId":1,"name":"Vector the Kind","rarityRank":1259,
  "unicode":{"textContent":["■▲▼▲▼▲","╱ ◎◎ ╱","╪","◈"],
             "colors":{"background":"hsl(98,20%,8%)","text":"hsl(278,85%,85%)"}},
  "traits":[{"trait_type":"Head","value":"▲▼▲▼▲"}, ...],
  "burnedAt":null,"burnedBy":null,
  "royalties":{"totalWei":"0","mintCount":0}}}

GET https://www.glyphbots.com/api/bot/1/story
{"story":{"arc":{"id":"shadow_stalker","title":"Stealth Infiltration Specialist",
   "role":"Security Bypass Expert","faction":"Void Syndicate",
   "mission":{"objective":"Bypass 8 security scanners to reach central data vault",
     "threat":"Patrol algorithms sweeping for intruders every 5 seconds", ...},
   "abilities":[{"name":"Shadow Merge","effect":"Become invisible in dark zones",
     "cooldown":"6 seconds"}, ...]}}}

GET https://www.glyphbots.com/api/bots/facets       trait values, and the glyph alphabet
GET https://www.glyphbots.com/api/artifacts/recent  collection-wide mint activity
```

The story is a separate endpoint from the bot, which is easy to miss: `/api/bot/1`
does not carry any of it. Every bot has an arc with a faction, a role, a mission
with an objective and a threat, and named abilities with cooldowns. That is a
character sheet, and it is what the pet is built on rather than a bare hunger bar.

A GlyphBot is not an image. It is four short lines of Unicode plus a foreground
and background color, which is already a display format for a 240x135 screen.

The collection's full alphabet is 105 distinct non-ASCII glyphs across all
11,111 bots, counted from `/api/bots/facets`. Stock ESP32 fonts carry none of
them, so the firmware ships a generated bitmap atlas of those 105. See the
atlas rule in [../CLAUDE.md](../CLAUDE.md).

`royalties.mintCount` and `burnedAt` are the only per-bot fields that change
over time on this endpoint. They are what drives the pet.

Mints are infrequent. The most recent across the whole collection was
2026-08-11, six days before this was written, which is why the pet is not fed
by them. See [ROADMAP.md](ROADMAP.md).

## coral

```
GET https://api.0xcoral.com/api/v1/resolve?q=MEME
{"query":"MEME","resolved":{"address":"0xb131f4A5...","chain":"ethereum","symbol":"MEME"}}

GET https://api.0xcoral.com/api/v1/tokens/{chain}/{address}
{"symbol":"MEME","market":{"marketCapUsd":31447771,"liquidityUsd":205368.53,
   "volume24hUsd":4207.44,"priceChange24hPct":-0.0101, ...},
 "holders":{"count":741291,"topHoldersExInfraPct":51.62, ...},"links":{...}}

GET https://api.0xcoral.com/api/v1/score/{chain}/{address}
{"score":61,"verdict":"unknown","confidence":0.649,"confidenceLabel":"medium",
 "explanation":{"headline":"Not enough corroborating signal for a confident read.",
   "bullets":["741305 holders · very deep base","top-10 hold 65% · moderately concentrated", ...],
   "caveats":["Not a price target, audit, or trading recommendation."]}}

GET https://api.0xcoral.com/api/v1/guess/daily
{"date":"2026-08-17",
 "clues":{"holderCount":741291,"liquidityUsd":205368.53,"marketCapUsd":31447771,
   "top10HolderShare":0.516,"uniqueBuyers24h":12,"uniqueSellers24h":9},
 "answer":{"score":61,"verdict":"unknown","confidenceLabel":"medium",
   "explanation":{"headline":"...","bullets":[...],"caveats":[...]}},
 "token":{"chain":"ethereum","address":"0xb131f4a5..."}}

GET https://api.0xcoral.com/api/v1/tokens/index?limit=3     corpus of graded tokens
GET https://api.0xcoral.com/api/v1/traction                 network aggregates
GET https://api.0xcoral.com/api/v1/dashboard                latest reef pulse, prose
GET https://api.0xcoral.com/api/v1/og/token/{chain}/{address}  302 to a rendered card
```

`resolve` is what makes a 56-key thumb keyboard usable here. Typing a 42
character contract address is miserable; typing `MEME` is not.

`tokens/{chain}/{address}` and `score/{chain}/{address}` split the game in half.
The first returns the market facts a person would reason from, the second
returns the answer. Show one, hide the other.

`explanation.bullets` come back as five short lines already sized for a narrow
column. The `caveats` array is not decoration, and the API sets an
`x-coral-attribution` header stating the same thing. Both get screen space.

Score is a heavy full lookup that self-rate-limits and took several seconds when
probed. It is request-response with a spinner, never a poll. Typing a ticker into
`/resolve` and scoring the result is the on-demand path.

`guess/daily` is the game path and it sidesteps all of that. One token a day, the
same for everybody, precomputed once into KV, so it answers from the edge instead
of waking the container. The clues are the market facts the score was computed
over; the answer ships in the same payload, so a device fetches one round and can
then play with the radio off. The token is deliberately unnamed.

There is deliberately no result-card endpoint. A rendered image only earns its
keep when someone shares a link and the unfurl needs a raster preview, which is
what `og/token` is for. A game result is not a page, so the share is text the
device composes itself from the round's `date` and `answer.score` plus the
player's own guess. That needs nothing from the API.

## voxels

```
GET https://www.voxels.com/api/womps.json?limit=1
{"success":true,"womps":[{
  "id":81301,
  "author":{"name":"The_Philosopher","owner":"0xe330..."},
  "image_url":"https://media.crvox.com/womps/0xe330.../womp_1787011729176_3a6d....jpg",
  "coords":"NE@3575E,1963S,3U",
  "parcel_address":"3 Snowman Palace",
  "parcel_island":"Chronos",
  "created_at":"2026-08-18T00:08:46.530Z"}]}
```

A womp is an in-world photo. The whole read API is unauthenticated GET and
well documented at [voxels.com/api](https://www.voxels.com/api): 6 womp routes,
7 wearable, 13 avatar, 20 parcel. Answers come wrapped as
`{"success":true,"<field>":...}`, and a failed lookup returns `success:false`
sometimes with status 200, so check the flag rather than the status.

`/api/womps/{id}.jpg` does not exist. The picture is whatever `image_url`
points at on `media.crvox.com`.

Those images are around 128KB. The device has 320KB of RAM in total with the
TLS stack already inside it, so this has to be a streaming block decode
straight to the display, never a fetch-then-decode. Scale down during the
decode rather than after.

Live presence is the one thing the API does not cover, and deliberately: the
docs say livekit, radio, metrics and the internal `/grid/*` routes are not
described. Do not scrape it.

## TLS roots

Two are enough for every host here, checked 2026-08-17:

- **Google Trust Services (WE1)**: gwei.ryanio.com, www.glyphbots.com,
  api.0xcoral.com, www.voxels.com
- **Let's Encrypt (YR2)**: media.crvox.com, which serves the womp images

`WiFiClientSecure` has `setCACertBundle`, so bundle both rather than pinning one
per host. Never ship `setInsecure()`.
