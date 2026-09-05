# Architecture

The SKSE hook replaces the `Actor::ProcessHit` call made by Skyrim 1.7.104.0
from the shared physical-hit dispatcher at Address Library ID 38627 plus
`0x4A8`. The eight bytes before the call, the direct-call opcode, and the nine
bytes after it must match the reviewed executable before the hook is installed.
The relative call target is allowed to have been redirected by another SKSE
plugin so normal trampoline chaining remains possible.

This site was selected from the complete normal-arrow control flow, not from a
callee match alone:

1. `ArrowProjectile` vtable slot `0xBE` resolves to Address Library ID 44204,
   the projectile `HandleHits` implementation.
2. Its owner-resolved actor branch calls a projectile dispatcher, which calls
   Address Library ID 38627.
3. ID 38627 builds the final `HitData` and calls `Actor::ProcessHit` at `+0x4A8`.
4. Control returns through `ArrowProjectile::HandleHits`; later,
   `ArrowProjectile::ProcessImpacts` reads the missile impact result and runs
   the visual embedding path only for `Stick`.

The previous hook at ID 44204 plus `0x3AA` called the same damage routine, but
only from the source-null fallback branch. Ordinary player and NPC arrows have
a resolved owner and branch around that site, which is why version 0.2.0 could
load successfully without ever changing their impacts.

The hook records whether the actor was alive and retains the projectile
reference already stored in `HitData`. It calls the original engine routine
first. Only after Skyrim applies damage does it classify the hit, read actual
remaining health, and run the pure decision policy. Melee hits traverse the
shared dispatcher too, but are rejected immediately unless `sourceRef` resolves
to an arrow-class projectile.

Target eligibility is conservative and keyword-driven. CommonLib's
`Actor::IsHumanoid()` must resolve Skyrim's default `ActorTypeNPC` keyword from
the current race or actor base, and a current race must exist. Applicable
actor/race keywords then reject every non-humanoid or
supernatural `ActorType` category shipped by Skyrim (`Animal`, `Cow`, `Creature`,
`Daedra`, `Dragon`, `Dwarven`, `Familiar`, `Ghost`, `Giant`, `Horse`, `Troll`,
and `Undead`); engine ghost and reanimated states are also rejected.
Default-object lookups are used for the canonical animal, Daedra, dragon,
Dwarven/robot, NPC, and undead categories; fixed Skyrim keyword FormIDs cover
`ActorTypeCreature` and `ActorTypeGhost`. This admits properly tagged mod-added
humanoids without relying on fragile skeleton
paths or a hard-coded list of playable races. The only race FormID exclusions
are Skyrim's `InvisibleRace` and `ManakinRace`, the two nonliving utility races
that otherwise carry only `ActorTypeNPC`. Missing or contradictory metadata
preserves vanilla behavior. Eligibility is captured before damage so death or
life-state transitions cannot reclassify the struck target.

Killing-blow detection deliberately precedes head/body threshold logic. The
hook records that the actor was alive, calls the synchronous engine damage
routine, then trusts the post-hit `Actor::IsDead()` result. It intentionally
does not infer death from the precomputed `HitData::kFatal` bit or raw zero
health: protected actors and death-prevention mechanics can survive either
signal. Skyrim 1.7.104 reports dying/dead actors through `IsDead()` while
essential-down actors remain nondead at this hook point.

Head/eye classification first uses Skyrim's `HitData::damageLimb`. Node names
from the projectile impact and a bounded ancestor walk are a fallback. Unknown
locations preserve vanilla behavior.

When the policy selects bounce, both the missile-level result and the matched
impact result are changed from an embedding result to `Bounce`. No damage fields,
actor values, projectile positions, inventory, forms, or save data are changed.

Default runtime telemetry is bounded to five first-occurrence messages per game
session. It records the first normal-arrow route, first matched policy decision,
first applied bounce, first missing-impact failure, and first handler exception.
Per-hit detail remains opt-in through `debugLogging`.
