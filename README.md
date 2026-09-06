# Conditional Arrow Embedding

Conditional Arrow Embedding is a focused SKSE plugin for Skyrim Special
Edition/Anniversary Edition runtime 1.7.104.0. It changes only whether a
successfully processed arrow-class projectile remains visually embedded after
hitting an actor. This includes arrows and crossbow bolts. It does not alter
projectile damage.

## Rules

- Conditional bounce decisions apply only to living humanoid NPC races. The
  actor must resolve through Skyrim's standard `ActorTypeNPC` keyword; animals, creatures, Daedra,
  dragons, Dwarven constructs, familiars, ghosts, giants, horses, trolls,
  undead, and reanimated actors preserve vanilla behavior.
- A nonlethal head or eye hit bounces instead of remaining embedded.
- A lethal head or eye hit preserves Skyrim's normal impact result, so it may
  remain embedded.
- A nonlethal body hit bounces while the target has 50% health or more after
  damage.
- A body hit may remain embedded once the target is below 50% health.
- A lethal body hit preserves Skyrim's normal impact result.
- Blocked hits, existing ricochets, non-ballistic projectiles, already-dead targets,
  and unsupported target types preserve vanilla behavior. An unclassified
  nonlethal hit bounces at 50% health or more; below that threshold it preserves
  vanilla behavior.

The plugin never forces an arrow to stick. It only changes an engine-selected
`Stick` or `Impale` result to `Bounce` when one of the rules above requires it.

## Requirements

- Skyrim executable 1.7.104.0
- SKSE 2.3.1
- Address Library for SKSE Plugins for runtime 1.7.104.0

The runtime and SKSE checks are intentionally exact. An unsupported executable
is rejected safely in the log rather than patched optimistically.

## Configuration

The configuration is `Data/SKSE/Plugins/ConditionalArrowEmbedding.json`.
`bodyStickBelowHealthRatio` defaults to `0.5`; exactly 50% still bounces, while
values below 50% may stick. Player and NPC handling can be toggled independently.
Debug logging is off by default.

Logs are written to the normal SKSE log directory as
`ConditionalArrowEmbedding.log`. The plugin has no modal error dialogs, audio,
focus changes, external processes, ESP/ESL, scripts, MCM, or save data.

Mod-added humanoids are supported when either the race or actor base follows
Skyrim's standard `ActorTypeNPC` tagging. Untagged or ambiguously tagged races fail open and keep
vanilla embedding rather than being guessed from skeleton or appearance. The
two nonliving vanilla utility races tagged as NPCs (`InvisibleRace` and
`ManakinRace`) are explicitly excluded.

The vanilla record evidence and classification rationale are documented in
[`docs/VANILLA-TARGET-AUDIT.md`](docs/VANILLA-TARGET-AUDIT.md).

## Compatibility

Version 0.3.3 distinguishes synchronous damage from Skyrim's copied-HitData
queue. It never decides from the pre-hit health returned by a queue submission.
Queued hits are evaluated after the actual damage callback, and a bounded gate
at the visual consumer prevents premature embedding/bounce while damage is
pending. Killing blows use actual engine death state, never predicted damage or
the precomputed fatal flag. Health and damage are not modified or replayed.

The four reviewed hook sites and original native targets must match. A changed
visual vtable or replaced damage call makes installation fail safely in the log;
compatibility with other hooks at these same sites requires a deliberate audit.
Mods that merely change arrow meshes, textures,
ammunition, damage, bows, perks, or actor health should generally be compatible.

Destroyed, already-processed, destroy-after-hit/explosive-path, chain-shatter,
stale or ambiguous impacts preserve vanilla. Queued tracking uses generation
handles and current-impact identity values, not owning pointers or serialized
state. It is capped at 256 entries and a two-second deadline; dropped/canceled
damage or an unusual ordering fails open rather than hanging the projectile.
No cross-save lifecycle guarantee or in-game acceptance is claimed yet.

This release is built only for 1.7.104.0 because the exact callsite and
surrounding instruction bytes were verified against that executable.

With normal logging, the DLL emits only first-occurrence runtime evidence: the
first arrow routed through the hook, first complete policy decision, first
conditional bounce, first missing-impact condition, first handler failure, and
the player's race and eligibility gates. Queue submission/completion, preserved
killing shots, visual deferral/commit, and rejected pending state are also
recorded once. This is bounded to at most twelve messages per game session. Enable debug logging
only when a per-hit trace is needed.

## Build and test

Initialize submodules, set `VCPKG_ROOT`, then run `tools/build.bat` from a Visual
Studio 2022 C++ environment. The build runs deterministic policy tests and a
binary audit. `tools/package.ps1` creates an installable archive and SHA-256
manifest.

`tools/verify-runtime-hook.ps1` additionally checks the installed 1.7.104.0
executable and Address Library. It proves the normal owner-resolved arrow route,
rejects the source-null regression site, follows queued damage to its consumer,
checks the visual gate's native false-return path, and verifies the handled-bit
guards that prevent damage replay. It does not assume queue/update ordering.

Policy tests prove the requested decision matrix independently of Skyrim. Native
layout tests additionally verify that the reanimation check reads the 1.7.104
runtime actor-state field, not the incompatible C++ base-class offset. A
successful loader smoke test proves SKSE initialization and hook installation;
actual arrow placement still requires in-game verification before a stable
release.

The native dispatch/gate tests cover consumer-before-damage and
damage-before-consumer ordering, full-health killing shots, surviving head/body
rules, strict threshold boundaries, requeues, identity changes, capacity and
timeouts. They exercise real first-party helpers with supplied health/death
fixtures, not Skyrim's damage engine. See
[`docs/QUEUED-DAMAGE-REGRESSION.md`](docs/QUEUED-DAMAGE-REGRESSION.md).

## License

Project-authored source is MIT licensed. See `THIRD-PARTY-NOTICES.md` and the
license files included with release artifacts for dependencies.
