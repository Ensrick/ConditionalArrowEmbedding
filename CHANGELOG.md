# Changelog

All notable changes follow Keep a Changelog. Versions use Semantic Versioning.

## [0.3.4] - 2026-09-05

### Fixed

- Admit ordinary non-hitscan arrows to queued impact tracking. Version 0.3.3
  incorrectly excluded every `kDestroyAfterHit` projectile, although Skyrim's
  arrow initialization sets that flag on normal ballistic arrows. The special
  ignored-return lifecycle actually requires a runtime explosion pointer.
- Keep actual explosion and chain-shatter paths vanilla without weakening
  processed/destroyed, handled-marker or complete impact-identity guards.

### Added

- Exact-runtime initialization/explosion-entry byte proofs and 16 native
  projectile-layout regression cases calling the production lifecycle helper.
- Bounded queued-registration telemetry with lifecycle flags and explicit
  capture rejection reasons instead of only an ambiguous missing-impact line.

This remains a test candidate. The 0.3.3 player log and executable inspection
establish the defect; 0.3.4 requires a fresh in-game visual/damage matrix.

## [0.3.3] - 2026-09-05

### Fixed

- Stop evaluating queued hits against pre-damage health. Skyrim 1.7.104's
  `Actor::ProcessHit` can copy/enqueue `HitData` and return without applying it;
  0.3.2 incorrectly treated that return as post-damage, causing killing shots
  from above 50% health to bounce.
- Hook the actual queued damage consumer and gate pending visual consumption
  until damage completes and Skyrim marks the impact's damage submitted.
  Native handled-bit checks prevent the resumed visual path replaying damage.
- Revalidate current impact identity before mutation; cap deferred state and
  fail open on timeout, ambiguity, destroyed/explosive paths, or changed impact.
- Keep native damage submission unconditional even if pre-hit bookkeeping
  throws; suppress diagnostic failures at native hook boundaries.

### Added

- Deterministic native callback-ordering tests and exact-runtime control-flow
  audits for queue command 0x10, its copied HitData, visual deferral and native
  damage-replay guards. Additional bounded runtime timing diagnostics.

This is a test candidate. Host tests and binary inspection pass; a fresh
in-game lethal/nonlethal visual-and-damage matrix remains required. Earlier
documentation describing ProcessHit as unconditionally synchronous was wrong
and is superseded by the queue-aware architecture below.

## [0.3.2] - 2026-09-05

### Fixed

- Read reanimation state through the runtime-aware `AsActorState()` accessor.
  The shipped multi-runtime 0.3.1 DLL read unrelated bytes at Actor+0xA8;
  1.7.104 stores the life-state word at Actor+0xC8. This could incorrectly
  exclude a living player from conditional arrow rules.
- Apply the high-health bounce rule even when hit location is unavailable:
  both a head hit and a body hit require bounce at 50% health or more.
  Unknown locations below the threshold still preserve vanilla behavior.

### Added

- 144 synthetic native-layout regression cases, coupled to the real eligibility
  and full-health player policy; separate player/NPC region-health-lethality matrix.
- One bounded player-specific telemetry line listing race and every eligibility
  gate, so a future exclusion has a diagnosable reason rather than a lone false flag.

This repair is supported by the user's 0.3.1 runtime log and compiled-offset
inspection. A fresh in-game visual/damage matrix is still required; native
layout tests and a successful build are not an in-game acceptance test.

## [0.3.1] - 2026-09-05

### Fixed

- Restrict collision mutation to the newly-created, unprocessed impact at the
  projectile impact-list head and require that it belongs to the struck actor.
  Older impact records are never scanned or used as a fallback.
- Use only the missile-wide result consumed by `ProcessImpacts` when deciding
  whether vanilla would embed, and reject already-processed or destroyed
  projectiles.
- Retain the projectile smart pointer across damage callbacks and require an
  exact `ArrowProjectile` cast.

## [0.3.0] - 2026-09-05

### Fixed

- Replace the unreachable source-null projectile fallback hook with the verified
  normal owner-resolved arrow damage path at Address Library ID 38627 plus
  `0x4A8`.
- Correct the native hook ABI to `void (Actor*, HitData&)`.
- Preserve post-damage killing-blow detection while changing only the impact
  result consumed later by `ArrowProjectile::ProcessImpacts`.
- Determine lethality from the engine's post-hit dead state instead of the
  precomputed fatal flag or zero health, preserving protected and
  death-prevention survivors.

### Added

- Add bounded first-occurrence runtime telemetry for arrow routing, policy
  decisions, applied bounces, missing impacts, and handler failures.
- Verify the complete 1.7.104.0 control-flow chain and reject reintroduction of
  the dead fallback hook.

## [0.2.0] - 2026-09-03

### Changed

- Limit conditional bounce behavior to living humanoid `ActorTypeNPC` races.
- Preserve vanilla embedding for undead, creatures, monsters, animals, Daedra,
  dragons, Dwarven constructs, ghosts, giants, trolls, familiars, horses, and
  reanimated actors.
- Give killing-blow detection precedence over head and health-threshold rules.

### Fixed

- Recognize lethal one-shots during the engine timing window where health has
  reached zero but `HitData::kFatal` or the final dead state is not yet visible.

### Tests

- Add regression coverage for every target exclusion, zero-health lethal
  fallback, nonlethal bleedout, and lethal-hit precedence.

## [0.1.0] - 2026-09-02

### Added

- Post-damage arrow embedding policy for nonlethal head hits and body-health
  threshold behavior.
- Exact Skyrim 1.7.104.0 and SKSE 2.3.1 runtime gates.
- Checked native callsite signature and fail-open handling for ambiguous impacts.
- JSON configuration and schema.
- Deterministic policy tests, local executable/address-library verification,
  binary audit, and reproducible packaging support.
