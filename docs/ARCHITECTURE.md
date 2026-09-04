# Architecture

The SKSE hook replaces the character-hit call made by Skyrim 1.7.104.0 from
`MissileProjectile::ProcessImpacts` at Address Library ID 44204 plus `0x3AA`.
The seven bytes before the call, the call opcode, and the five bytes after it
must match the reviewed executable before the hook is installed.

The hook records whether the actor was alive and retains the projectile reference
already stored in `HitData`. It calls the original engine routine first. Only
after Skyrim applies damage does it classify the hit, read actual remaining
health, and run the pure decision policy.

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

Killing-blow detection deliberately precedes head/body threshold logic. It
combines the post-hit `HitData::kFatal` bit, the engine dead query, dying/dead
life states, and a finite health-at-or-below-zero fallback. Essential-down,
bleedout, and unconscious life states override ambiguous fatal/dead signals
because they are explicitly nonlethal. This closes the timing gap where a
one-shot has depleted health but the fatal flag or final dead state is not yet
observable.

Head/eye classification first uses Skyrim's `HitData::damageLimb`. Node names
from the projectile impact and a bounded ancestor walk are a fallback. Unknown
locations preserve vanilla behavior.

When the policy selects bounce, both the missile-level result and the matched
impact result are changed from an embedding result to `Bounce`. No damage fields,
actor values, projectile positions, inventory, forms, or save data are changed.
