# Killing-hit regression: queued damage timing

Status: 0.3.4 test candidate, **runtime acceptance open** (issue #1).

## Evidence

The user's September 5, 2026 playtest of installed 0.3.2 reports arrows bouncing
on one-shot kills and killing hits against victims starting above 50% health.
The real SKSE log shows 0.3.2 loading at 20:03:53, and at 20:10:33 its first
matched living humanoid body hit reports `postHitHealthRatio=1.0000`,
`killedByHit=false`, missile result 4 and action `bounce`. This log alone does
not identify whether that particular first hit was lethal, but it agrees with
the causal timing defect found in the exact executable.

SkyrimSE.exe 1.7.104.0 SHA-256:
`846efccf0c1374d71f892907f46549560f2fcb0a75cb87a3eed438baa0f1402f`.

| Exact RVA | Reviewed behavior |
| --- | --- |
| `6CA6D1` | ProcessHit asks whether damage must be deferred |
| `6CA6E7` | Calls queue producer `667D80` |
| `6CA6EC` | Jumps to exit `6CB7C2`, bypassing actual damage body |
| `667DC8` | Constructs command type `0x10` with actor reference |
| `667E28` | Copies 0x90-byte HitData through copy constructor `7EF890` |
| `667E3B` | Enqueues copied command through `66BFC0` |
| `66A35D` | Command 0x10 consumer calls actual ProcessHit |
| `800D7E` | HandleHits marks the impact's damage submitted (`unk48=1`) |
| `7DDEC0` | Arrow visual consumer; stick side effect starts at `7DDF11` |
| `7FA8A3` | Normal projectile update invokes visual consumer |
| `7FA8AB` | False visual return jumps to no-cleanup function exit |
| `7FAA6B`, `7FAB50` | Common impact processing skips both native damage paths for handled impacts |
| `8029DA` | Special runtime-explosion caller ignores return; excluded from waiting |

The old label "post-damage" was therefore false on the queue-submission branch.
Using estimated final damage, raw health zero, or the predicted fatal flag would
not repair that timing contract and could misclassify essential/protected actors.

## 0.3.3 player-sticking regression

The subsequent real log identifies 0.3.3 loading at 22:25:21 on September 5.
At 22:30:32 it records player `00000014`, race `00013741`, living-humanoid
eligibility true with every exclusion false, followed by queued arrow damage.
At 22:30:46 it reports a missing/changed/processed/unsupported queued impact
and preserves vanilla. It contains no successful policy or visual-gate decision.
The log cannot distinguish which guard failed, but rules out the earlier
player eligibility fault for that recorded hit.

Exact-executable inspection identifies a concrete new exclusion in 0.3.3:
`CaptureImpactStamp` rejected `kDestroyAfterHit` unconditionally, but that flag
is set on ordinary non-hitscan arrows. Arrow vtable `[0xC0]` resolves to
`7DE270`; at `7DE287` its initialization calls `7F5090`. `7F50C1` loads base
projectile flag bit 0 (hitscan), and `7F5187` through `7F51A5` sets runtime bit
15 when hitscan is false while mirroring hitscan into bit 13. Thus normal
ballistic arrows were never registered in the queued gate.

The ignored-return caller at `8029DA` is inside `802580`, whose entry requires
the runtime explosion pointer at projectile `+0x158`; a null pointer branches
from `8025C6` to the early return at `80262E` through `802655`. Version 0.3.4
therefore excludes actual runtime-explosion and chain-shatter lifecycles,
not bit 15 alone. Processed/destroyed flags, handled markers, exact stamps,
deadlines and original damage dispatch remain unchanged. No new hook is added.

The new native regression varies hitscan, bit 15, chain-shatter and explosion
independently over 16 synthetic 1.7.104 layouts, calling the production helper.
Exact-byte audit checks bind this rule to arrow initialization and the explosion
entry guard. These establish the code defect and repair; they do **not** prove
that all runtime acceptance cases pass. Bounded registration success/failure
messages now include flags, explosion presence and the precise capture guard.

## Repair

The producer is observed before enqueue, and marks the active thread-local hit
scope deferred. That scope never makes a premature decision. The actual queued
consumer runs the original damage routine exactly once, then evaluates real
health/death state unless it has deferred again. It stores a visual decision
against a validated current-impact stamp.

No natural ordering between queue drain and projectile updates is assumed.
The visual wrapper returns false while tracked damage is pending or the native
handled marker is absent. The reviewed normal caller simply returns, and the
next visual call can resume after completion. Keeping the native handled marker
intact prevents replaying damage when the original consumer finally runs.

The shared pure policy is unchanged: actual killing hits preserve vanilla;
nonlethal heads bounce; bodies bounce at/above 50% actual post-hit health;
creatures/undead and policy-ineligible hits preserve vanilla. Godmode remains
eligible, with its real unchanged health controlling nonlethal behavior.

## Safety and limits

- Both completion and visual consumption validate generation-bearing handles,
  exact current list head, node, collision vectors, and original result values.
  Saved pointer values are compared only, never dereferenced after callbacks.
- The gate holds no persistent strong engine references, changes no damage
  fields, and has a fixed 256-entry capacity and two-second deadline.
- Ambiguous duplicate impacts, canceled queues, changed/destroyed projectiles,
  capacity overflow and expiry fail open. Actual runtime-explosion and
  chain-shatter arrows are vanilla, because delaying their special lifecycle
  has not been proved safe.
- Pre-hit bookkeeping exceptions cannot skip native damage. Hook-local
  diagnostic failures are contained; engine exceptions are not retried.
- Replaced hook targets are refused before any hook is installed. Other SKSE
  plugins altering these exact control-flow boundaries need an explicit audit.
- There is no serialized data and no explicit save-load reset callback. Handle
  and identity checks plus expiry are defensive measures, not cross-load proof.

## Verification

`tools/verify-runtime-hook.ps1` includes `verify-queued-damage.ps1` and asserts
the exact instruction paths above against the installed executable, as well as
the production source wiring. The audit's JSON explicitly marks gameplay as
unverified. Five CTest groups cover policy, 144 synthetic actor-state layouts,
16 projectile lifecycle layouts, source wiring, and real dispatch/gate helpers
with supplied health/death fixtures.

The ordering tests cover synchronous damage, actual queue submission without an
early decision, both queue/visual orders, completion-before-handled-marker,
full-health lethal head/body/unknown hits, survivors, strict threshold,
creature exclusions, nested/thread-local scopes, requeues, stale stamps,
capacity, expiry and preparation exceptions. They are not a Skyrim simulator.

Required runtime matrix: controlled lethal and nonlethal head/body hits, victims
starting both above and below 50%, a crossing-threshold nonlethal hit, full-health
godmode player, nonhumanoids, blocked hits, arrows and crossbow bolts. Compare
actual damage with/without the DLL. Confirm no repeated damage, no lingering
projectiles, and correct visual results. Do not close issue #1 from a build or
these host tests alone.
