# Changelog

All notable changes follow Keep a Changelog. Versions use Semantic Versioning.

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
