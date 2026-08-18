# Roadmap

Arduino + PlatformIO on three Cardputer ADVs. Endpoints in [API.md](API.md),
rules in [../CLAUDE.md](../CLAUDE.md).

The ADV has hardware the original lacked: a BMI270 IMU, an ES8311 codec driving
a real 1W speaker, a decent MEMS mic, and a 1750mAh battery. Phases 3 and 5 are
built around those rather than around the screen.

## Phase 0: skeleton

- `pio run -t upload`, boot, WiFi from `include/secrets.h`, clock via SNTP.
- Three-item menu (Gas / Bot / Reef). Every view exits with `` ` `` (Esc).
- Battery percentage in a corner, always.

Done when: it boots, you can move between three empty views and back out.

## Phase 1: gas, which is really the network stack

Smallest payload, so it proves HTTPS + JSON before anything depends on them.

- `WiFiClientSecure` + ArduinoJson. `GET /api/gas` every 30s, matching the
  server's own refresh window.
- Base fee large, three tiers under it. `ethPriceUsd: null` is its own state,
  never a zero.
- Background banded by congestion (gwei's own Chill / Busy / Chaos / Whale).
- `GET /api/gas/history` as a 240px sparkline. Points arrive thinned to one a
  minute already.
- **Threshold alarm through the ES8311.** Type a number, it plays a tone when
  base fee drops under it. A real speaker, so use it: a short rising arpeggio,
  not a beep.

Done when: it sits on the desk showing live gas and wakes you when gas is cheap.

## Phase 2: OTA, before the phases that need iterating

Small, and it pays for itself immediately: every phase after this one is edited
far more than it is designed.

- `ArduinoOTA` or `esp_https_ota` against a versioned binary. Device checks on
  boot and on demand, never on a timer.
- Keep the previous image and roll back on a failed boot. Three units and no
  cable means a bad push is otherwise three walks to the desk.
- Show the running version on the menu, so "did it take" is answerable by
  looking.

Done when: you push a build and all three units are running it without a cable.

## Phase 3: the glyph atlas

The only part with real unknowns.

- Generator fetches `GET /api/bots/facets`, extracts the distinct non-ASCII
  glyphs across every trait value (105 as of 2026-08-17), renders each to a mono
  bitmap, emits a C header plus a codepoint lookup.
- Size the cell against the screen, not the font. Four lines on 135px means
  roughly 24x24 with room for a name and rank.
- `GET /api/bot/{tokenId}`, render `unicode.textContent` through the atlas,
  `unicode.colors` converted from `hsl()` to RGB565.

Done when: three units each hold a different bot and look good enough to hand
someone.

## Phase 4: the pet, which is where the ADV earns its keep

Settle one thing first: **real collection activity is too slow to be the food
supply.** The most recent mint across the whole collection was 2026-08-11. A pet
fed on onchain events starves for a week. So attention is the food, and real
activity is the rare event.

- Identity from `GET /api/bot/{tokenId}/story`, cached to microSD: faction,
  role, mission, named abilities with cooldowns. The pet is that character.
- State in NVS: hunger, mood, energy, age, streak. Decays on wall clock, so it
  runs with WiFi off and through sleep.
- **Every bot has a voice.** Derive a short signature tone from the bot's trait
  glyphs, so #5815 always sounds like #5815 and two bots are audibly different.
  A collection you can hear is a thing nobody has.
- **The IMU is the interaction.** Pick it up and the pet wakes. Shake it and the
  pet objects. Tilt to pet it. Set it face down and it sleeps. No keyboard.
- **The mic is ambient mood.** Sample the room's noise floor, not its content. A
  bot in a loud room is livelier; one on a quiet desk gets bored. Never record,
  never transmit, just a level.
- Poll `GET /api/bot/{tokenId}` every few minutes for the two fields that move:
  `royalties.mintCount` and `burnedAt`.
- A mint against your bot is a celebration: its own tone, animation, a permanent
  line on the card. Rare by design, which is why it lands.
- `burnedAt` non-null is death. Permanent, onchain, not resettable from the
  device. A pet that can really die is why anyone cares about this one.
- Magnetic back: it lives on the side of a monitor and you poke it in passing.

Done when: it changes across a day of being ignored, and you can tell whose bot
is whose with your eyes shut.

## Phase 5: the Coral round

`GET /api/v1/guess/daily` does the work server-side: one token a day, same for
everyone, market facts as the clue and the score as the answer, one payload.

- One fetch a day, then it plays with the radio off.
- Anonymous, no ticker. You reason from holders, liquidity, mcap and the
  buyer/seller split. That is the difference between a judgment call and
  recognizing a name.
- Type 0 to 100. Reveal score, verdict, `explanation.bullets`. Streak in NVS.
- Every reveal carries `explanation.caveats` and the Coral name.
- Free play: type a ticker into `/resolve` and score it. Live and slow, needs a
  spinner.

Done when: a round is genuinely hard and you want another.

## Phase 6: three of them

- ESP-NOW, no router. Mind the shared radio: a peered unit and a fetching unit
  want different channels, so pick one per app state.
- **Bots meet.** Bring two close, they trade tokenId, traits and rank. Shared
  traits give a compatibility, both get a mood boost, each remembers the other.
  Bots that have met before greet each other by playing the other's tone. This
  is the mode people film.
- **Head to head.** Everyone already has the same daily token, so comparing
  guesses needs no coordination at all.
- Three units, so it is a tournament, not a duel.

Done when: two bots meeting produces something you would show a stranger.

## Phase 7: off the device

- The share is text. The device has the date, `answer.score` and the guess.

  ```
  Coral daily 2026-08-17
  I said 61 · it was 48

  0xcoral.com
  ```

  Two numbers and a date, copy-pasteable, reads fine with no image. An OG card
  was built for this and deleted: an image only earns its keep when someone
  shares a link and the unfurl needs a raster preview, and a result is not a
  page.
- A QR gets it off the device, pointing at a prefilled post.

## Phase 8: Ask Coral

The keyboard's real purpose. Type a question, get an answer.

Coral has no public ask endpoint today, and the existing pipeline cannot be
wrapped: `HandleAskCoralArgs` wants a tenant, channel, platform and message id,
and the result is `{ path, sent, skippedReason }`. It SENDS through platform
egress and never returns text. So this needs a parallel path through the model
that returns instead of sending.

Spend rails first, because an unauthenticated public AI endpoint is an open
wallet:

- Its own tight per-IP bucket on the existing limiter.
- A hard global answers-per-day ceiling that declines politely once hit.
- A low max-tokens per answer, which the screen wants anyway.
- Behind a test, per the money rule.

On the device:

- Terse mode. 240x135 is roughly eight short lines, so the endpoint returns a
  short answer, not a truncated long one.
- Carries its caveats and the Coral name, same contract as every other score
  surface.
- **Hand off to Telegram for the long version.** A QR or deep link continues the
  same question in the bot, where there is room to answer properly. The device
  gives you the gist; the phone gives you the rest.

## Phase 9: a script layer

Compiled C++ always needs a flash. An interpreter does not.

- Embed something small (Lua is the obvious candidate) with a narrow API: draw,
  fetch, tone, read a key. Not the whole firmware.
- Type a script on the device and it runs immediately. This is what Phase 2's
  OTA cannot give you.
- The same capped Coral endpoint from Phase 8 can return a script instead of an
  answer, so "write me a behavior" works without a model key on the device.
- Sandbox it before it is fun: network allowlist, no filesystem writes, a
  watchdog, and a revert-to-last-good. The first bad script otherwise wedges the
  UI and the only fix is a cable.

## Parked

Ideas the hardware allows that nothing yet needs. Left here rather than built.

- IR emitter: no use. It is a TV blaster, and the stock firmware already has one.
- BLE: the bot as a beacon nearby phones can see. Fun, no clear payoff.
- 3.5mm jack: only matters if the pet gets a soundtrack.
- LEGO holes: a three-unit desk dock, when there are three finished units.
- Voice input: the mic is good enough, but there is no on-device speech to text,
  and streaming audio out is a privacy surface this does not need. Phase 8 takes
  typing.

## Constraints worth keeping in view

- 240x135. Four lines of large text or roughly eight small. Design for eight.
- 56 keys, no comfortable way to type 42 hex characters. Anything needing an
  address goes through `/resolve` with a ticker.
- M5Cardputer 1.1.1+ or the TCA8418 keyboard reads nothing.
- Phases 1 and 3 both want all-day battery, so sleep behavior is not a Phase 7
  problem.
