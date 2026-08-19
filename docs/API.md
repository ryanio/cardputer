# The five sources

All five are public, unauthenticated JSON over HTTPS, so no key ever reaches
the device and there is nothing to rotate. Bodies below are trimmed to the
fields the firmware reads.

[tools/apicheck/check.py](../tools/apicheck/check.py) asks all of them again on
demand and every morning in CI, asserting these fields and invariants over TLS
trusted by the exact roots in `src/ca_roots.h`. A source that changes shape, or
rotates onto a root we do not carry, fails there first. `--save` writes each
answer into `sim/fixtures`, which is what the simulator serves.

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

GET https://www.glyphbots.com/api/bots/search?limit=6&sort=rarity&cursor=11105
{"bots":[{"tokenId":9200,"name":"Binary the Unstoppable","rarityRank":3,
          "traits":[...],"unicode":{...},"burnedAt":null}, ...],
 "total":11111,"nextCursor":null}

GET https://www.glyphbots.com/api/artifacts/recent
{"ok":true,"items":[{"botTokenId":5815,"title":"Photon Emerging From The Neon Fracture",
   "mintedAt":"2026-08-11T06:25:37.929Z","mintQuantity":137,
   "imageUrl":"...","minter":"0x4A30...","type":"character"}, ...]}
```

`search` takes exactly one sort, `rarity`, and runs it from the most ordinary
bot towards rank 1, so the rarest page is the tail of it: `cursor` is a plain
offset and `total` comes back on every answer, which is what the cursor for
that page is counted from. Anything else passed as `sort` is ignored and the
default applies, which is token id descending. A cursor past the end answers
with an empty list and the true `total` rather than an error. Every hit already
carries the art and the rank, so a list costs one request.

`artifacts/recent` is mint activity, not sales: nothing public prices a bot.
Ten items, no `limit`, and they are sparse enough that the same bot appears
several times over a few weeks.

The story is a separate endpoint, easy to miss: `/api/bot/1` carries none of
it. Every bot has a faction, a role, a mission with an objective and a threat,
and three abilities. Two cost time and carry a `cooldown`; the third costs a
`resource` instead and has no cooldown field at all.

A GlyphBot is not an image. It is four lines of Unicode, never more than seven
characters wide, plus two colors, which is already a display format for a
240x135 screen and is what makes a 32 pixel cell the largest that fits.

**The colors come in two forms.** Two of every three bots state them as
`#rrggbb` and the rest as `hsl()`, so a parser that knows one draws a bot
nobody can see. The art also carries the odd ASCII character, a caret or a
lower case o, which the panel's own font has.

There is a rendered PNG per bot at
`https://media.glyphbots.com/bots/pngs/{tokenId}.png`, 3000x2250, which is what
an unfurl shows. The device never fetches one: it is a raster of four lines of
text it already has. It matters only as the layout reference, where it pins the
monospace advance, the centering and the line pitch.

The alphabet is 105 non-ASCII glyphs across all 11,111 bots, counted from
`/api/bots/facets`, which
[tools/glyphs/generate.py](../tools/glyphs/generate.py) reads directly so a
trait added later fails a check rather than drawing a blank square. Stock ESP32
fonts carry none of them.

`royalties.mintCount` and `burnedAt` are the only per-bot fields that change
over time, and mints are infrequent: the most recent across the collection was
2026-08-11. See [ROADMAP.md](ROADMAP.md) for why the pet is not fed by them.

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

GET https://api.0xcoral.com/api/v1/tokens/index?limit=3     corpus of graded tokens, {chain, address}
GET https://api.0xcoral.com/api/v1/traction                 network aggregates
GET https://api.0xcoral.com/api/v1/dashboard                latest reef pulse, prose
GET https://api.0xcoral.com/api/v1/og/token/{chain}/{address}  302 to a rendered card
```

`resolve` is what makes a 56-key thumb keyboard usable here. Typing a 42
character contract address is miserable; typing `MEME` is not.

`tokens/{chain}/{address}` and `score/{chain}/{address}` split the game in half.
The first returns the market facts a person would reason from, the second
returns the answer. Show one, hide the other.

`explanation.bullets` are five short lines already sized for a narrow column.
`caveats` is not decoration, and the API repeats it in an
`x-coral-attribution` header. Both get screen space.

Score is a heavy lookup that self rate limits and takes seconds. It is request
and response behind a spinner, never a poll, and a 429 means wait rather than
retry.

`tokens/index` returns just `{chain, address}` per entry, which makes a random
round a single extra fetch: pick one, then score it. Together with `resolve`
that gives the Reef view three ways in: a ticker someone types, a random token
from the graded corpus, and the token of the day everybody shares.

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

## bankr

```
GET https://api.bankr.bot/agent-profiles?limit=5
{"profiles":[{"slug":"surplus-intelligence","projectName":"Surplus Intelligence",
  "tokenSymbol":"Surplus","tokenName":"...","tokenAddress":"0xc52aedec...","tokenChainId":"base",
  "marketCapUsd":3143563.615,"vol24hUsd":...,"weeklyRevenueWeth":"1.568400",
  "description":"...","twitterUsername":"...","website":"...","profileImageUrl":"...",
  "productsCount":3,"tags":[...],"createdAt":"..."}, ...],
 "total":113,"limit":5,"offset":0}

GET https://api.bankr.bot/agent-profiles/{slug}          one profile
GET https://api.bankr.bot/agent-profiles/{slug}/tweets   recent posts
```

113 agents ordered by market cap. `limit` and `offset` are both honoured and
`limit` refuses anything over 100. This is the one Bankr surface needing no
key, which is the only reason the device can read it: the Agent and Wallet APIs
are `X-API-Key` and would put a secret on a unit we give away.

**Almost every profile carries `tokenAddress` and a `tokenChainId`, which is
what Coral's score endpoint takes.** That cross is the point of the view: Bankr
says which agents are earning, Coral says what its own read of the token is.
Two things the shape does not promise: 12 of the 113 are on `robinhood` rather
than `base`, and one carries no token at all, with `tokenAddress`,
`tokenSymbol` and `marketCapUsd` absent rather than null. Coral answers for
both chains.

Bankr keys can be scoped `readOnly` with an IP allowlist if a personal view is
ever wanted. Typed on the device, never compiled in. Not built.

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

A womp is an in-world photo. The read API is unauthenticated GET and well
documented at [voxels.com/api](https://www.voxels.com/api). Answers wrap as
`{"success":true,"<field>":...}` and a failed lookup returns `success:false`,
sometimes with a 200, so check the flag rather than the status.

`/api/womps/{id}.jpg` does not exist. The picture is whatever `image_url`
points at on `media.crvox.com`.

`limit` works and `offset` is ignored, so browsing backwards means taking the
newest id and walking down: `GET /api/womps/{id}.json` resolves any single one
and ids are sequential. No popularity or view count in the payload, so
"notable" is not something this API can answer.

Images are 45KB to 152KB, always 1024x1024, against 320KB of RAM with the TLS
stack inside it. M5GFX handles this: `drawJpg(Stream*, ...)` wraps the body and
the decoder pulls a few bytes at a time, holding nothing but its own 3.9KB work
pool. A zoom of -1 on the width fills the panel and keeps the aspect, cropping
a square womp rather than leaving a 135 pixel picture between black bars.

Those overloads exist only when `Stream_h` and `ARDUINO` are both defined,
which cannot be made true in the simulator without swapping its panel driver.
Hence `src/jpeg.cpp` and `sim/src/jpeg_sim.cpp`.

Live presence is deliberately not covered: livekit, radio, metrics and the
internal `/grid/*` routes are undocumented. Do not scrape them.

## TLS roots

Two are enough for every host here, checked against the live chains 2026-08-17:

- GTS Root R4 anchors Google Trust Services WE1: gwei.ryanio.com,
  www.glyphbots.com, api.0xcoral.com, www.voxels.com
- ISRG Root YR anchors Let's Encrypt YR2: media.crvox.com, which serves the
  womp images

Both ship in `src/ca_roots.h`, which carries the fetch and verify commands for
refreshing them. mbedtls parses concatenated PEM, so one `setCACert` call takes
the set and no host is pinned. Never ship `setInsecure()`.

Two more ride along. `api.bankr.bot` sits behind Amazon, not Google, so it
needs **Amazon Root CA 1** and would have failed TLS without it. And for the
updater rather than for any source here:
`api.github.com` chains to Sectigo Public Server Authentication Root E46, so a
version check would fail TLS without it. The release download itself lands on
ISRG Root YR, which the womp images already needed.
