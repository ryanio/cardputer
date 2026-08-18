# Roadmap

Build order for the three-Cardputer firmware. Each phase ends with something
you can hold and show someone. Phases 0 through 2 are the first evening.

Endpoint shapes and verified response bodies are in [../README.md](../README.md).

## Phase 0: plumbing, disguised as the gas clock

The point is WiFi, TLS, JSON and one screen working end to end. The gas app is
the vehicle because it is the smallest payload of the three and needs no fonts.

- Boot, join WiFi from `include/secrets.h`, set the clock.
- `GET /api/gas` every 30s, matching the server's own refresh window.
- Base fee large, three speed tiers under it, ETH price in the corner. Handle
  `ethPriceUsd: null` as its own state rather than printing zero.
- Background color banded by congestion, reusing gwei's own Chill, Busy, Chaos
  and Whale thresholds.
- `GET /api/gas/history` on a longer interval, drawn as a 240px sparkline.
  Points arrive thinned to one a minute already.
- Threshold alarm: type a number, the speaker beeps when base fee drops under
  it. This is the part that earns desk space.

Done when: a unit sits on the desk showing live gas and beeps at a threshold.

## Phase 1: the glyph atlas and a bot on screen

The atlas is the only part of the project with real unknowns, so it comes early
while the plumbing is fresh.

- `tools/glyph-atlas/` fetches `GET /api/bots/facets`, extracts the distinct
  non-ASCII characters across every trait value (105 as of 2026-08-17), renders
  each from a source font into a mono bitmap, and emits a C header plus a
  codepoint lookup table.
- Pick the cell size against the screen, not against the font. Four lines of
  bot on 135px of height means roughly 24x24 with room for a name and rank.
- `GET /api/bot/{tokenId}`, render `unicode.textContent` through the atlas with
  `unicode.colors` converted from `hsl()` to RGB565.
- Name, rarity rank, and a trait list on a second screen.

Done when: three units each boot holding a different bot, and they look good
enough that you want to hand one to someone.

## Phase 2: the bot as a pet

A tamagotchi needs state that moves. One thing to get right before writing any
of it: **real collection activity is too slow to be the food supply.** The most
recent artifact mint across the whole collection was 2026-08-11, six days before
this was written. A pet fed only by onchain events starves for a week at a time.

So the loop is inverted. Attention is the food, and real activity is the rare
event that matters.

- Local state in NVS: hunger, mood, energy, age, care streak. Decays against
  wall clock, so it keeps running with WiFi off and keeps running while the
  device sleeps.
- Care actions on the keyboard, a few seconds each, several times a day. This
  is the everyday loop and it is entirely offline.
- Poll `GET /api/bot/{tokenId}` every few minutes for the two fields that
  actually change: `royalties.mintCount` and `burnedAt`.
- A mint against your bot is a celebration: sound, animation, a permanent
  entry in the bot's history on SD. Rare by design, and worth noticing because
  of it.
- `burnedAt` going non-null is death. Permanent, onchain, not resettable from
  the device. A pet that can really die is the reason anyone will care about
  this one.
- Idle animation driven by the glyph atlas. Swapping the eyes and mouth cells
  between trait-adjacent glyphs is enough to read as alive; no new art needed.

Done when: the pet visibly changes across a day of not touching it, and a real
mint produces a real reaction.

## Phase 3: the Coral game

The shareable unit here is the score, so the game is built around guessing it.

`GET /api/v1/tokens/{chain}/{address}` returns symbol, market cap, liquidity,
holder count, 24h volume and price change. `GET /api/v1/score/{chain}/{address}`
returns the answer, 0 to 100, with an explanation. Show the first, hide the
second, make the player guess. Those are exactly the facts a person would
reason from, which is what makes it a real game rather than a coin flip.

- Corpus from `GET /api/v1/tokens/index`, cached to SD as a question bank.
- A round shows the ticker and its market card. Player types a number 0 to 100.
- Reveal the real score, the verdict, and `explanation.bullets`. Points by how
  close, streak tracked in NVS.
- **Prefetch one round ahead and cache every score to SD.** The score endpoint
  is heavy and self-rate-limits. A round must never block on a live call, and
  three units playing at once must never look like an attack.
- Every reveal screen carries `explanation.caveats` and the Coral name. Not
  optional; see the display contract in [../CLAUDE.md](../CLAUDE.md).

Done when: a round is genuinely hard to guess and you want to play again.

## Phase 4: three units, ESP-NOW

This is what the third Cardputer is for, and every mode here is the reason to
own more than one.

- Peering over ESP-NOW, no router involved. Mind the shared radio and channel;
  a peered unit and a fetching unit want different things.
- **Bots meet.** Bring two units close and they exchange tokenId, traits and
  rarity rank. Shared traits compute a compatibility, both bots get a mood
  boost, and each records having met the other. Bots that have met before
  recognize each other. This is the mode people will actually film.
- **Head to head Coral.** Same token, both players guess, closest wins. Three
  units makes it a tournament.
- Sync the question bank over the link so only one unit spends the API calls.

Done when: two bots meeting produces something you would show a stranger.

## Phase 5: getting it off the device

Physical device photos do well on X on their own, so most of this is making the
screen worth photographing. The rest is one hop.

- A result screen designed to be photographed: final score, streak, bot, big
  and legible at arm's length.
- QR on screen pointing at `GET /api/v1/og/token/{chain}/{address}`, which 302s
  to a rendered Coral card. Scan with a phone, share the card, and the post
  unfurls with real Coral artwork instead of a blurry screen photo.
- The device never posts anything. It displays a code; a human decides.

Done when: playing a round produces something worth posting without editing it.

## Later

- On-device WiFi setup so `include/secrets.h` can be deleted.
- Battery and sleep tuning, since phases 0 and 2 both want all-day runtime.
- IR transmitter has no use yet. Leave it alone until it does.
