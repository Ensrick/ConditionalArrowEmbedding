# Architecture

The SKSE hook replaces the character-hit call made by Skyrim 1.7.104.0 from
`MissileProjectile::ProcessImpacts` at Address Library ID 44204 plus `0x3AA`.
The seven bytes before the call, the call opcode, and the five bytes after it
must match the reviewed executable before the hook is installed.

The hook records whether the actor was alive and retains the projectile reference
already stored in `HitData`. It calls the original engine routine first. Only
after Skyrim applies damage does it classify the hit, read actual remaining
health, and run the pure decision policy.

Head/eye classification first uses Skyrim's `HitData::damageLimb`. Node names
from the projectile impact and a bounded ancestor walk are a fallback. Unknown
locations preserve vanilla behavior.

When the policy selects bounce, both the missile-level result and the matched
impact result are changed from an embedding result to `Bounce`. No damage fields,
actor values, projectile positions, inventory, forms, or save data are changed.
