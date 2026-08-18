# Roadmap

Build order for the Coral app on the Cardputer ADV. Each phase ends with
something you can hold and show someone.

Endpoint shapes and verified response bodies are in [API.md](API.md). The fork
and its constraints are in [../CLAUDE.md](../CLAUDE.md).

## Phase 0: on the menu

Prove the toolchain before writing a feature. Nothing here is Coral-specific.

- Clone the UserDemo, `CardputerADV` branch, `python3 ./fetch_repos.py`,
  `idf.py build`, flash it unmodified. Confirm the stock firmware still boots.
- Copy `main/apps/app_dummy/` to `main/apps/app_coral/`, rename the class,
  add the include to `main/apps/apps.h`, register it in `main/main.cpp` with
  `GetMooncake().installApp(std::make_unique<AppCoral>());`.
- Draw a title and a three-item sub-menu (Gas, Bot, Reef). Nothing behind them
  yet.
- Wire G0 so it closes the app from the sub-menu, and from inside any view once
  those exist. Test it before moving on: an app you cannot leave is the one bug
  guaranteed to annoy you every session after.

Done when: Coral appears in the launcher between the stock apps, opens, shows
three choices, and G0 gets you out.

## Phase 1: gas, which is really the network stack

The smallest payload of the three, so it is the one that proves HTTP, TLS and
JSON parsing.

- `esp_http_client` + `esp_crt_bundle` for TLS, cJSON for parsing. All in
  ESP-IDF, nothing to add.
- `GET /api/gas` every 30s, matching the server's own refresh window.
- Base fee large, three speed tiers under it, ETH price in the corner. Handle
  `ethPriceUsd: null` as its own state rather than printing zero.
- Background banded by congestion, reusing gwei's Chill / Busy / Chaos / Whale
  thresholds.
- `GET /api/gas/history` on a longer interval as a 240px sparkline. Points
  arrive thinned to one a minute already.
- Threshold alarm through the ES8311 codec: type a number, it beeps when base
  fee drops under it. The ADV's audio path is a real speaker, so this is worth
  doing properly.

Done when: a unit sits on the desk showing live gas and beeps at a threshold.

## Phase 2: the glyph atlas and a bot on screen

The only part with real unknowns, so it comes while the plumbing is fresh.

- Tooling fetches `GET /api/bots/facets`, extracts the distinct non-ASCII
  characters across every trait value (105 as of 2026-08-17), renders each into
  a mono bitmap, and emits a C header plus a codepoint lookup table.
- Pick the cell size against the screen, not the font. Four lines of bot on
  135px of height means roughly 24x24 with room for a name and rank.
- `GET /api/bot/{tokenId}`, render `unicode.textContent` through the atlas with
  `unicode.colors` converted from `hsl()` to RGB565.

Done when: three units each hold a different bot, and they look good enough that
you want to hand one to someone.

## Phase 3: the bot as a pet

One thing to settle before writing any of it: **real collection activity is too
slow to be the food supply.** The most recent artifact mint across the whole
collection was 2026-08-11, six days before this was written. A pet fed only by
onchain events starves for a week at a time.

So the loop is inverted. Attention is the food; real activity is the rare event.

- Identity from `GET /api/bot/{tokenId}/story`, fetched once and cached to
  microSD: faction, role, mission and named abilities with cooldowns. The pet is
  that character. The cooldowns are the obvious hook for care actions.
- Local state in NVS: hunger, mood, energy, age, care streak. Decays against
  wall clock, so it keeps running with WiFi off and through sleep.
- Care actions on the keyboard, seconds at a time, several times a day. Entirely
  offline.
- Poll `GET /api/bot/{tokenId}` every few minutes for the two fields that change:
  `royalties.mintCount` and `burnedAt`.
- A mint against your bot is a celebration: sound, animation, a permanent entry
  on the card. Rare by design, and worth noticing because of it.
- `burnedAt` going non-null is death. Permanent, onchain, not resettable from the
  device. A pet that can really die is why anyone will care about this one.
- Idle animation from the atlas: swapping eye and mouth cells between
  trait-adjacent glyphs reads as alive without new art.
- The BMI270 is here, so picking the device up can wake the pet.

Done when: the pet visibly changes across a day of being ignored, and a real mint
produces a real reaction.

## Phase 4: the Coral round

`GET /api/v1/guess/daily` does the hard part server-side: one token a day, the
same for everybody, market facts as the clue and the score as the answer, in one
precomputed payload.

- One fetch a day. The whole round arrives together, so a unit plays with the
  radio off afterwards.
- The round is anonymous, no ticker. The player reasons from holder count,
  liquidity, market cap and the buyer/seller split, which is the difference
  between a judgment call and recognizing a name.
- Player types 0 to 100. Reveal the real score, the verdict and
  `explanation.bullets`. Score by closeness, streak in NVS.
- Every reveal screen carries `explanation.caveats` and the Coral name.
- For free play beyond the daily, type a ticker into `/resolve` and score the
  result. That path is live and slow, so it needs a spinner.

Done when: a round is genuinely hard to guess and you want to play again.

## Phase 5: three units

What the other two Cardputers are for.

- ESP-NOW peering, no router. Mind the shared radio and channel: a peered unit
  and a fetching unit want different things.
- **Bots meet.** Bring two close and they exchange tokenId, traits and rarity
  rank. Shared traits compute a compatibility, both get a mood boost, and each
  records having met the other. Bots that have met before recognize each other.
  This is the mode people will film.
- **Head to head.** Everyone already has the same daily token, so two units
  comparing guesses needs no coordination at all.

Done when: two bots meeting produces something you would show a stranger.

## Phase 6: getting a result off the device

- The share is text, not an image. The device has everything it needs: the
  round's date, `answer.score`, and the player's own guess.

  ```
  Coral daily 2026-08-17
  I said 61 · it was 48

  0xcoral.com
  ```

  Two numbers and a date. Copy-pasteable, quotable, and it reads fine in a post
  with no image attached, which is how Wordle's share worked. A rendered card was
  built for this and deleted: an OG image only earns its keep when someone shares
  a link and the unfurl needs a raster preview, and a game result is not a page.
- A QR is still the way off the device, pointing at a prefilled post.

Done when: finishing a round produces something worth posting without editing it.

## Later

- Rebase onto ADV-V0.3 or whatever is current. Keeping our diff to one app
  directory plus three lines is what makes that cheap.
- Battery and sleep tuning. 1750mAh is generous, but Phases 1 and 3 both want
  all-day runtime.
- The IR emitter and the microphone have no use yet. Leave them until they do.
