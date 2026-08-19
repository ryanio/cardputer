# Roadmap

Arduino + PlatformIO on three Cardputer ADVs. Endpoints in [API.md](API.md),
rules in [../CLAUDE.md](../CLAUDE.md).

The ADV has hardware the original lacked: a BMI270 IMU, an ES8311 codec driving
a real 1W speaker, a decent MEMS mic, and a 1750mAh battery. Phases 3 and 5 are
built around those rather than around the screen.

**Where it stands.** Phases 0, 1, 3, 4 and 6 are built, and 6 was pulled
forward because Coral is the story. Phase 2, OTA, is the only piece of the
original plan still unwritten. Nothing here has run on hardware: see the flash
gate in [PLAN.md](PLAN.md), which nobody has been through yet.

## Phase 0: skeleton

- `pio run -t upload`, boot, clock via SNTP.
- Five-item menu (Gas / Bot / Reef / Womp / Setup). Every view exits with
  `` ` `` (Esc).
- Battery percentage in a corner, always.
- **WiFi is typed on the device.** Scan, pick, type the passphrase, and it goes
  to NVS. A compiled network only ever belongs to a dev unit: these get given
  away, and a gifted unit should join its new owner's network rather than carry
  the last owner's. It also keeps the passphrase out of the binary, which
  anyone can read back off the flash with a cable.

**Built.** Eight cards rather than five, none of them empty.

## Phase 1: gas, which is really the network stack

Smallest payload, so it proved HTTPS and JSON before anything else depended on
them. Base fee large, three tiers, a 24h sparkline, a threshold alarm through
the ES8311, `GET /api/gas` every 30s to match the server's own window.

**Built.** Two departures from this plan. Congestion is a percentile of the
day rather than the four named constants, because the API publishes no bands
and base fees have moved by two orders of magnitude inside a year. The
sparkline is logarithmic, because one spike otherwise owns the whole height.
The arpeggio has never been heard.

## Phase 2: OTA, before the phases that need iterating

**Not built**, and the only piece of the original plan that is not. Everything
that would benefit from iterating is already written, so this is now simply
what makes the next flash cheap.


Small, and it pays for itself immediately: every phase after this one is edited
far more than it is designed.

- `esp_https_ota` against a binary attached to a **GitHub release**. Device
  checks on boot and on demand, never on a timer. `gh release create` is the
  publish step, so there is no bucket to run.
- Two hosts, two roots, both already in `src/ca_roots.h`: `api.github.com` for
  the version check, which chains to Sectigo Public Server Authentication Root
  E46, and `release-assets.githubusercontent.com` for the download, which
  chains to ISRG Root YR. Checked live 2026-08-17.
- **Verify a signature before installing.** TLS proves the bytes came from
  GitHub, not that you built them, so an account compromise serves a malicious
  image over a perfectly valid connection. These units live in other people's
  houses and update themselves, which is exactly the case where that matters.
  Sign the image at release time with ECDSA P-256, publish the signature beside
  the binary, and check it against a public key compiled into the firmware
  before the image is marked valid. mbedtls is already linked, so this costs a
  hash and a verify. The private key never touches the repo or a device.
- An image that fails the signature check is not installed, and the device says
  which check failed rather than "update failed".
- Keep the previous image and roll back on a failed boot. Three units and no
  cable means a bad push is otherwise three walks to the desk.
- Show the running version on the menu, so "did it take" is answerable by
  looking.

Done when: you push a build and all three units are running it without a cable,
and an image signed with the wrong key is refused.

## Phase 3: the womp frame

The cheapest fun in the plan: Phase 1's network stack plus pictures. The JPEGs
run 45KB to 152KB against 320KB of RAM with TLS in it, so the decode has to
stream block by block straight to the panel, scaling as it goes.

**Built**, apart from the cycling. The decode goes through M5GFX's own
`drawJpg(Stream*)` and holds nothing but its 3.9KB work pool; whether that
survives beside a live TLS session is unknown 3 and has never run on a unit.

Done when: a unit on the shelf quietly cycles the newest pictures in Voxels.

## Phase 4: the glyph atlas

The only part that had real unknowns.

Every bot has a canonical render at
`media.glyphbots.com/bots/pngs/{tokenId}.png`, 3000x2250, and that is how
anyone who has seen a GlyphBot has seen it. The device never fetches one, but
it is the layout reference: a monospace advance, a space as a full empty cell,
every line centered whatever its length, and never more than four lines of
seven cells.

**Built.** `tools/glyphs/generate.py` reads the 105 non-ASCII glyphs from the
collection's facets endpoint and renders them at one size on a shared baseline
into 32x32 cells, which is the largest that fits four lines of seven on the
panel. Two redistributable fonts cover them between them, DejaVu Sans and
Unifont, since no system font here had all 105.

Two things the plan had wrong. The colors are `#rrggbb` about twice as often as
`hsl()`, and the art carries the occasional ASCII character, which the panel's
own font draws. The question of which font the PNG generator uses turned out
not to matter: the site ships a four glyph subset of Fira Code and leaves the
rest to each viewer's OS, so there is no canonical face to match.

The part about three units is not built.

Done when: three units each hold a different bot and look good enough to hand
someone.

## Phase 5: the pet

Three things make it a pet: it changes while ignored, you interact by picking it
up, and it can really die. Everything else is a later pass.

Settle one thing first: **real collection activity is too slow to be the food
supply.** The most recent mint across the whole collection was 2026-08-11. A pet
fed on onchain events starves for a week. So attention is the food, and real
activity is the rare event.

- Identity from `GET /api/bot/{tokenId}/story`, cached to microSD: faction,
  role, mission, named abilities with cooldowns. The pet is that character.
- State in NVS: hunger, mood, energy, age, streak. Decays on wall clock, so it
  runs with WiFi off and through sleep.
- **The IMU is the interaction.** Pick it up and the pet wakes. Shake it and the
  pet objects. Tilt to pet it. Set it face down and it sleeps. No keyboard.
- Poll `GET /api/bot/{tokenId}` every few minutes for the two fields that move:
  `royalties.mintCount` and `burnedAt`.
- A mint against your bot is a celebration: its own tone, animation, a permanent
  line on the card. Rare by design, which is why it lands.
- `burnedAt` non-null is death. Permanent, onchain, not resettable from the
  device. A pet that can really die is why anyone cares about this one.
- Magnetic back: it lives on the side of a monitor and you poke it in passing.

Done when: it changes across a day of being ignored, and burnedAt ends it.

## Phase 6: the Coral round

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

**Built**, ahead of phases 1 to 5, because Coral is the story. The streak
counts from the dates the rounds carry rather than the device clock, so it
holds on a unit whose clock never landed.

Done when: a round is genuinely hard and you want another.

## Phase 7: three of them

Two tiers, because a unit you gave away is not in the room.

### Same room: ESP-NOW

Peer to peer on the 2.4GHz radio, no router, no internet. Payloads cap at ~250
bytes, which a tokenId plus a few traits fits inside with room spare. Broadcast
needs no pairing.

- **Bots meet.** Bring two close, they trade tokenId, traits and rank. Shared
  traits give a compatibility, both get a mood boost, each remembers the other.
  Bots that have met before greet each other by playing the other's tone.
- **Head to head.** Both units already hold the same daily token, so comparing
  guesses needs no handshake at all. Three units makes it a tournament.
- Mind the shared radio: ESP-NOW and WiFi want one channel between them, so a
  peered unit and a fetching unit are doing incompatible things. Pick one per
  app state.

### Passive: BLE advertising

The sleeper feature for units that get given away. A Cardputer can notice
another one nearby without pairing, without anyone opening a menu, without
either person doing anything. Your bot meets a stranger's bot because both were
in a bag at the same event. Cheap to add, and it is the encounter mechanic that
works when neither owner is paying attention.

### Anywhere: already free

The daily round serves everyone the same token, so two people on different
continents are playing the same puzzle with no server coordination. Comparing
results needs nothing built. That is the property that makes a gifted unit still
feel connected to yours.

What is NOT free, and is a decision rather than a diff: persistence. A
leaderboard, or bots remembering an encounter across devices, both need a write
surface on Coral. Neither is planned.

### Not available

IR. The ADV has an emitter and no receiver, so it can blast a TV but two
Cardputers cannot talk over it. Same on the original.

Done when: two bots meeting produces something you would show a stranger, and a
unit you gave away still shares your daily puzzle.

## Phase 8: off the device

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

## Not here: Ask Coral and a script layer

Both are Coral backend work that a device happens to consume: a new public
endpoint, spend rails (per-IP bucket, hard daily ceiling, low max-tokens), and a
model path that RETURNS instead of sending. `HandleAskCoralArgs` wants a tenant,
channel, platform and message id and answers `{ path, sent, skippedReason }`, so
none of it can be wrapped.

Planned in the coral repo, not here. The device consumes the endpoint when it
exists, and the long answer hands off to Telegram because 240x135 is about eight
lines.

## Parked

Ideas the hardware allows that nothing yet needs. Left here rather than built.

- IR emitter: no use. It is a TV blaster, and the stock firmware already has one.
- 3.5mm jack: only matters if the pet gets a soundtrack.
- LEGO holes: a three-unit desk dock, when there are three finished units.
- Voice input: the mic is good enough, but there is no on-device speech to text,
  and streaming audio out is a privacy surface this does not need.
- Per-bot signature tones, derived from trait glyphs so each bot is audibly
  itself. The most original idea here, and the one most likely to eat a week.
- Ambient mic mood: sample the room's noise floor, never its content. Needs
  real-room testing, so it waits for finished units.

## Constraints worth keeping in view

- 240x135. Four lines of large text or roughly eight small. Design for eight.
- 56 keys, no comfortable way to type 42 hex characters. Anything needing an
  address goes through `/resolve` with a ticker.
- M5Cardputer 1.1.1+ or the TCA8418 keyboard reads nothing.
- Phases 1 and 3 both want all-day battery, so sleep behavior is not a Phase 7
  problem.
