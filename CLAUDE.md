# cardputer

Firmware for three M5Stack Cardputers reading the public APIs of coral,
glyphbots and gwei. `AGENTS.md` is a symlink to this file.

Stack and endpoint shapes live in [README.md](README.md). Build order lives in
[plans/ROADMAP.md](plans/ROADMAP.md). Read both before editing.

## Operating Contract

1. **Think before coding.** Read the closest doc first. State assumptions when
   they matter.
2. **Simplicity first.** Smallest change that solves the request. This is an
   embedded target with one screen and three apps; no abstraction unless it
   removes real complexity now.
3. **Surgical changes.** Every changed line traces to the ask. Do not churn
   unrelated files.
4. **Verify on hardware.** A change that compiles is not a change that works.
   Flash it and look at the screen before calling it done.

## Don't re-derive these

- **No secrets ever reach the device.** All three APIs are public and
  unauthenticated. If a task seems to need a key, the design is wrong. WiFi
  credentials are the sole exception and they live in gitignored
  `include/secrets.h`, then in NVS once the setup screen exists. Never commit
  an SSID or password, and never log one.
- **One root CA covers every host.** gwei.ryanio.com, www.glyphbots.com and
  api.0xcoral.com all present Google Trust Services WE1 certificates. Bundle
  the GTS root and pin nothing else. Never ship `setInsecure()`; use it while
  bringing a host up, and delete it in the same session.
- **Respect the upstream cache windows.** gwei refreshes its snapshot at most
  once per 30s, so poll no faster. Coral's `/api/v1/score` is a heavy full
  lookup with its own stricter rate limiter; call it on demand or on prefetch,
  never in a loop, and cache every result to SD. A firmware that hammers a
  rate-limited endpoint gets three devices blocked at once.
- **The glyph atlas is generated, never hand-edited.** `tools/glyph-atlas/`
  reads the live alphabet from `GET https://www.glyphbots.com/api/bots/facets`
  and emits the C header. Editing the header directly means the next
  regeneration silently drops the edit. Regenerate and commit both.
- **Coral output carries its caveats.** The `/api/v1/score` response includes
  an `explanation.caveats` array and the API sets an `x-coral-attribution`
  header. Any screen showing a score shows the caveat and the Coral name too.
  This is a display contract, not a nicety, and it applies to the game screens
  and the shareable card equally.
- **A GlyphBot is text, not an image.** Render from `unicode.textContent` and
  `unicode.colors`. Never fetch or decode a PNG for a bot.

## Hardware constraints worth remembering

- 240x135. Four lines of large text, or roughly eight of small. Design for the
  smaller number.
- `hsl(...)` strings come from the API and the display wants RGB565. Convert in
  `lib/ui`, not at each call site.
- The keyboard has 56 keys and no comfortable way to type 42 hex characters.
  Any flow needing a contract address goes through coral's `/api/v1/resolve`
  with a ticker instead.
- ESP-NOW and WiFi share one radio and one channel. A unit that is peered and
  a unit that is fetching are doing incompatible things unless the channel is
  managed deliberately. Pick one per app state.
- Wall-clock time survives deep sleep only if it was set. Fetch time once from
  a response header or SNTP at boot; pet decay depends on it.

## Verification

Use the narrowest check that covers the change.

```bash
pio run                 # compile
pio run -t upload       # flash the connected unit
pio device monitor      # serial log
```

Docs-only changes need neither. Anything touching rendering, timing or the
network path gets flashed and looked at.

## Commits

Commit at meaningful checkpoints, not only when asked. Scope each commit to its
own ask. Commit onto `main` directly, no branches. `[no-deploy]` is meaningless
here; there is no deploy.

Open a PR instead when the change touches the TLS path, the secrets handling,
or anything that would put load on a rate-limited upstream.
