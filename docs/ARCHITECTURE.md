# Architecture

The SKSE implementation has four coordinated hook sites for Skyrim 1.7.104.0:

| Site | Purpose |
| --- | --- |
| ID 38627 + `0x4A8` | Ordinary physical hit: evaluate only after non-deferred damage |
| ID 38586 + `0xA7` | Actual queue submission: mark the dispatch deferred before enqueue |
| ID 36991 + `0xA3D` | Queued command 0x10: evaluate after actual damage processing |
| Arrow vtable ID 209891, slot `0xAC` | Delay pending visuals; commit a completed decision |

Reviewed instruction signatures, Address Library RVAs and original call/vtable
targets must all match before any hook is written. Replaced targets are rejected
instead of assuming another hook preserves the required timing contract.

This site was selected from the complete normal-arrow control flow, not from a
callee match alone:

1. `ArrowProjectile` vtable slot `0xBE` resolves to Address Library ID 44204,
   the projectile `HandleHits` implementation.
2. Its owner-resolved actor branch calls a projectile dispatcher, which calls
   Address Library ID 38627.
3. ID 38627 builds `HitData` and calls `Actor::ProcessHit` at `+0x4A8`.
4. ProcessHit may apply damage now, or copy/enqueue command 0x10 and return
   without damage. The latter runs through ID 36991 + `0xA3D` later.
5. `HandleHits` sets the impact's handled byte after damage **submission**.
6. `ArrowProjectile::ProcessImpacts` reads the missile result. Its wrapper waits
   when a tracked hit has not completed damage or has not acquired the handled
   marker yet, then passes the final result into the original visual consumer.

The previous hook at ID 44204 plus `0x3AA` called the same damage routine, but
only from the source-null fallback branch. Ordinary player and NPC arrows have
a resolved owner and branch around that site, which is why version 0.2.0 could
load successfully without ever changing their impacts.

The hook records whether the actor was alive and retains the projectile
reference already stored in `HitData`. It calls the original engine routine
first. A thread-local scope observes the exact enqueue branch, so returning
from a producer is never mislabeled post-damage. Only a non-deferred damage
completion reads actual remaining health and runs the pure policy. Melee hits traverse the
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

Reanimation state is read via `Actor::AsActorState()->IsReanimated()`. A direct
call to the inherited `Actor::IsReanimated()` is invalid in this multi-runtime
build: 0.3.1 compiled it to read Actor+0xA8, while 1.7.104's ActorState base is
Actor+0xC0 and its life-state word is Actor+0xC8. The synthetic native regression
varies both words independently and verifies the actual helper and decision.

Killing-blow detection deliberately precedes head/body threshold logic. The
hook records that the actor was alive, calls the engine damage routine, and
only after a non-deferred completion trusts `Actor::IsDead()`. It intentionally
does not infer death from the precomputed `HitData::kFatal` bit or raw zero
health: protected actors and death-prevention mechanics can survive either
signal. Skyrim 1.7.104 reports dying/dead actors through `IsDead()` while
essential-down actors remain nondead at this hook point.

Head/eye classification first uses Skyrim's `HitData::damageLimb`. Node names
from the projectile impact and a bounded ancestor walk are a fallback. Unknown
locations below 50% health preserve vanilla behavior; at 50% or more both
possible region rules require bounce, so no location evidence is needed.

When policy selects bounce, both missile and matched impact results change to
`Bounce`: immediately for synchronous damage, or at the guarded visual consumer
for queued damage. Matching is limited to the current impact-list head. The
handled byte distinguishes damage submission from projectile-wide completed
visual processing; queued completion accepts handled 0/1, while the visual
gate requires 1 before resuming. Native `Projectile::ProcessImpacts` checks that
byte before either actor-damage path, preventing a resumed visual callback from
applying the hit twice. The mod never writes or clears it.

The queued identity stamp retains only generation-bearing projectile and actor
handles, compared pointer values, collision vectors, node identity and both
original results. Cached pointers are never dereferenced. Both completion and
consumption must match the live stamp, including the exact list head.
Historical impacts are never searched or used as a fallback, and
already-processed or destroyed projectiles fail open. Policy
eligibility is based on the missile-wide result because that is the value
`ProcessImpacts` actually dispatches; the per-impact value is synchronized only
when applying a bounce. No damage fields,
actor values, projectile positions, inventory, forms, or save data are changed.

Pending state is mutex-protected, fixed at 256 records and expires after two
seconds of steady-clock time. Duplicate ambiguous submissions are rejected;
an actual consumer requeue keeps its original deadline. The map holds no engine
object references and serializes nothing. Canceled, stale, capacity-limited or
expired hits preserve vanilla. Destroy-after-hit/chain-shatter paths never wait,
because a special native caller destroys them regardless of the visual return.
Explicit save-load invalidation is not implemented; current handle/stamp guards
and expiry are not a claim of tested cross-load lifecycle behavior.

Default runtime telemetry is bounded to twelve first-occurrence messages per
session, including classification, damage submission/completion and visual
gating. Detailed per-hit logging is opt-in. Host tests and the exact executable
audit establish source/ABI/control-flow properties, not actual gameplay
acceptance. See `QUEUED-DAMAGE-REGRESSION.md` for the failure evidence and limits.
