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
  unsupported target types, and impacts whose location cannot be classified
  preserve vanilla behavior.

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

The hook runs after Skyrim applies the prepared `HitData` to a character. It
uses the resulting health and life state only to recognize a killing blow; it
does not modify health, damage, or replace the hit. Mods that also
patch the same call in `MissileProjectile::ProcessImpacts` may conflict at the
native-code level. Mods that merely change arrow meshes, textures, ammunition,
damage, bows, perks, or actor health should generally be compatible.

This first release is built only for 1.7.104.0 because the exact callsite and
surrounding instruction bytes were verified against that executable.

## Build and test

Initialize submodules, set `VCPKG_ROOT`, then run `tools/build.bat` from a Visual
Studio 2022 C++ environment. The build runs deterministic policy tests and a
binary audit. `tools/package.ps1` creates an installable archive and SHA-256
manifest.

Policy tests prove the requested decision matrix independently of Skyrim. A
successful loader smoke test proves SKSE initialization and hook installation;
actual arrow placement still requires in-game verification before a stable
release.

## License

Project-authored source is MIT licensed. See `THIRD-PARTY-NOTICES.md` and the
license files included with release artifacts for dependencies.
