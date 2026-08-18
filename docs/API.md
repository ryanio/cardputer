# The five sources

Every endpoint below was probed live on 2026-08-17, and
[tools/apicheck/check.py](../tools/apicheck/check.py) asks all of them again on
demand and every morning in CI. It asserts the fields and the invariants the
firmware reads, over TLS trusted by the exact roots in `src/ca_roots.h`, so a
source that changes shape or rotates onto a root we do not carry fails there
first. `--save` writes each answer into `sim/fixtures`, which is what the
simulator serves.

 Response bodies are trimmed
to the fields the firmware reads.

All five are public, unauthenticated JSON over HTTPS. No API key ever reaches
the device, so there is nothing to leak in a flashed binary and nothing to
rotate.

Two roots cover every host between them, including the CDN the womp images come
from. See [TLS roots](#tls-roots) at the end.

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
with an objective and a threat, and three named abilities. Two of them cost
time and carry a `cooldown`; the third costs a `resource` instead and has no
cooldown field at all, which is easy to miss and is checked for. That is a
character sheet, and it is what the pet is built on rather than a bare hunger bar.

A GlyphBot is not an image. It is four short lines of Unicode plus a foreground
and background color, which is already a display format for a 240x135 screen.
Always four lines, never more than seven characters wide, which is what makes a
32 pixel cell the largest that fits the panel.

**The colors come in two forms.** Sampled across the collection, two of every
three bots state them as `#rrggbb` and the rest as `hsl()`, so a parser that
knows only one draws a bot nobody can see. The art also contains the odd ASCII
character, a caret or a lower case o, which the panel's own font already has.

There is a rendered PNG per bot at
`https://media.glyphbots.com/bots/pngs/{tokenId}.png`, 3000x2250, and it is
what an unfurl or a marketplace shows. The device never fetches one, since that
is a 3000x2250 raster of four lines of text it already has. It matters as the
reference the glyph atlas is measured against on a host, where it pins the
advance, the line pitch and the centering. See [ROADMAP.md](ROADMAP.md).

The collection's full alphabet is 105 distinct non-ASCII glyphs across all
11,111 bots, counted from `/api/bots/facets`, which
[tools/glyphs/generate.py](../tools/glyphs/generate.py) reads directly so a
trait added later shows up as a missing glyph rather than a blank square. Stock ESP32 fonts carry none of
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

`explanation.bullets` come back as five short lines already sized for a narrow
column. The `caveats` array is not decoration, and the API sets an
`x-coral-attribution` header stating the same thing. Both get screen space.

Score is a heavy full lookup that self-rate-limits and took several seconds when
probed. It is request-response with a spinner, never a poll. Typing a ticker into
`/resolve` and scoring the result is the on-demand path.

`tokens/index` returns just `{chain, address}` per entry, which makes a random
round a single extra fetch: pick one, then score it. Together with `resolve`
that gives the Reef view three ways in, a daily round everyone shares, a ticker
someone types, and a random token from the graded corpus.

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

Probed live 2026-08-18: 113 agents, ordered by market cap, `limit` and `offset`
both honoured, and `limit` refuses anything over 100. This is the one Bankr surface that needs no key, which is the
only reason the device can read it: the Agent and Wallet APIs are `X-API-Key`
and would put a secret on a unit we give away.

**Almost every profile carries `tokenAddress` and a `tokenChainId`, which is
exactly what Coral's score endpoint takes.** Two things the shape does not
promise: 12 of the 113 are on `robinhood` rather than `base`, and one profile
has no token at all, so `tokenAddress`, `tokenSymbol` and `marketCapUsd` are
absent rather than null. Coral answers for both chains. A view that assumes
every row has a token draws a blank line for that one. That cross is the point of the
view: Bankr says which agents are earning, Coral says what its own read of the
token is. Scoring one took 6.6s when probed, so it needs a spinner and it
carries the caveats like any other score.

If a personal view is ever wanted, Bankr keys can be scoped `readOnly` with an
IP allowlist, so a leaked one cannot move money. Typed on the device, never
compiled in, same as WiFi. Not built.

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

`limit` works and `offset` is ignored: asking for offset 3 returns the same
three newest womps. But `GET /api/womps/{id}.json` resolves any single womp and
ids are sequential, so browsing backwards means taking the newest id and
walking down. There is no popularity or view count in the payload, so "notable"
is not something this API can answer.

Those images are around 128KB. The device has 320KB of RAM in total with the
TLS stack already inside it, so this has to be a streaming block decode
straight to the display, never a fetch-then-decode. Scale down during the
decode rather than after.

Live presence is the one thing the API does not cover, and deliberately: the
docs say livekit, radio, metrics and the internal `/grid/*` routes are not
described. Do not scrape it.

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
