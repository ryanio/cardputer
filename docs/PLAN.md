# Engineering plan

Execution plan for Phases 0 to 3 of [ROADMAP.md](ROADMAP.md). Scope was cut to
those four deliberately; everything past Phase 3 waits for feedback from real
hardware.

## The constraint that shapes everything

**Nobody in the build loop can flash.** Agents and the manager can compile;
only Ryan can put a binary on a device and look at it. So an agent's definition
of done is "compiles clean", never "works", and wide fan-out is the wrong move:
it produces unverified firmware faster than it can be verified, and unverified
firmware compounds.

Hence a small team, sequenced around flash-and-look gates.

## Stage 1: the spine (serial, manager)

Everything depends on it, so it is not delegated.

```
src/net.{h,cpp}    WiFi join, HTTPS GET with the CA bundle, fetch-and-parse JSON
src/ui.{h,cpp}     title, big number, status bar, battery, hsl -> RGB565
src/view.{h,cpp}   view registry, menu, input loop, the exit convention
src/store.{h,cpp}  NVS settings
src/ca_roots.h     the two roots, and the commands to refresh them
src/version.h      one string, compared against the release tag
src/views/*.cpp    a placeholder per view, so the menu is walkable now
```

Headers are frozen at the end of this stage. Stage 2 cannot start before that,
because frozen headers are the only thing keeping three parallel agents off each
other.

## Stage 2: three agents in parallel

Run through `/subagent-team`. One directory each, no shared files beyond the
spine's headers.

| Agent | Owns | Delivers |
|-------|------|----------|
| A | `src/views/gas.*` | Two endpoints, sparkline, congestion banding, alarm tone |
| B | `src/views/womp.*` | womps.json, streaming JPEG decode, caption |
| C | `src/ota.*` | Version check against a GitHub release, download, rollback, version on the menu |

Agents A and B replace a placeholder that is already there, and a view reaches
the menu through `VIEW_REGISTER` rather than through a shared list.

Each agent runs `pio run` and stops. No agent flashes, and no agent edits the
spine; a spine change is a request back to the manager.

## Stage 3: integration (manager)

Merge, resolve, keep `pio run` green, then write the flash gate: what to do,
what you should see, and what failure looks like. A hardware check should take
two minutes, not a debugging session.

## Flash gates

| After | You check |
|-------|-----------|
| Stage 1 | Menu appears, four entries, every one exits. Serial prints free heap, whether PSRAM exists, and whether one TLS fetch lands. |
| Agent A | Live gas, correct banding, alarm fires at a typed threshold. |
| Agent C | A pushed release installs itself, and a deliberately broken build rolls back. |
| Agent B | A womp draws without an allocation failure. |

## Unknowns only hardware can settle

1. **Is there PSRAM?** M5 does not list any for the Stamp-S3A and the first
   build reported 320KB, which reads as internal SRAM alone. Decides whether
   Phase 3 is easy or fiddly.
2. **Does TLS fit** beside the display buffer in 320KB? The Phase 1 risk.
3. **Does streaming JPEG decode hold** at ~128KB per image? Phase 3's core bet.

The Stage 1 gate answers 1 and 2. It prints the heap on an otherwise empty
skeleton, then makes one fetch when WiFi lands, which costs a boot and saves
agent A a cycle. Sequence 1 before 3 for that reason.

## Not in this plan

Phases 4 to 8 wait for hardware feedback. Ask Coral and the script layer are
coral-side work and are planned there.
