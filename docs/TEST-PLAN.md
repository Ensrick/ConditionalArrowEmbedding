# In-game test plan

Use a disposable save and test with debug logging enabled.

Before launching, run `tools/verify-runtime-hook.ps1`. It must report
`sourceContractVerified=true`, `reachableFromOwnerResolvedArrowPath=true`, and
`verified=true` for the exact local executable and Address Library.

1. Shoot a healthy living humanoid in the head without killing it: damage applies and the
   arrow does not remain embedded.
2. Kill a living humanoid from full health with one headshot: damage applies and
   vanilla may retain the arrow. Repeat with debug logging and confirm
   `killedByHit=true` even when the pre-hit health was above 50%.
3. Shoot an actor above and exactly at 50% post-hit health in the torso: damage
   applies and the arrow does not remain embedded.
4. Shoot an actor below 50% post-hit health in the torso: vanilla embedding is
   preserved.
5. Kill an actor with a body shot: vanilla embedding is preserved.
6. Block an arrow and test an existing ricochet effect: vanilla behavior remains.
7. Confirm the player and ordinary human, elf, Orc, Khajiit, and Argonian races
   use the conditional rules. Reproduce the reported failure with a full-health
   Breton player in godmode receiving NPC arrows: arrows must bounce and the
   bounded player eligibility line must show `livingHumanoid=true`,
   `reanimated=false`. Repeat without godmode; it is not an eligibility exception.
8. Confirm draugr, skeletons, vampires, ghosts, reanimated corpses, animals,
   Falmer/creatures, Daedra, dragons, giants/trolls, and Dwarven constructs all
   preserve vanilla embedding at both high and low remaining health.
9. Test at least one mod-added living humanoid whose race carries
   `ActorTypeNPC`, plus one untagged custom race; the former is eligible and the
   latter fails open.
10. Test arrows from mod-added ammunition.
11. Repeat representative cases with crossbow bolts; confirm spells and other
   non-ballistic projectiles retain vanilla behavior.
12. Inspect `ConditionalArrowEmbedding.log` and the crash logger after the matrix.
    The first ordinary arrow must produce `first normal arrow reached`, the first
    matched hit must produce `first matched arrow decision`, and an eligible
    nonlethal test must produce `first conditional bounce applied` for synchronous
    damage or `queued post-damage bounce committed at visual consumer` for queued
    damage. A queued submission must not produce a decision until actual damage
    completes. A lethal queued test must report `killing arrow preserved` and
    `action=preserve-vanilla` (not a pre-hit full-health bounce). With debug
    logging disabled, none of those categories may repeat during the session.
13. Repeat above-half-health one-shot and multi-shot kills in both head and body.
    Compare per-hit health loss with the DLL disabled: no duplicate damage or
    altered damage is allowed. Include a nonlethal body shot crossing from
    above 50% to below 50%; it must use the lower actual post-hit health.
14. Confirm no projectile lingers after queued damage. A rare visual deferral is
    allowed until damage finishes; canceled/ambiguous paths preserve vanilla.
    Special explosive/destroy-after-hit and chain-shatter arrows remain vanilla.
15. Reload the disposable save and repeat a short matrix; explicit cross-save
    lifecycle acceptance remains unverified until this is tested.

Run `ActorStateAccessTests` before packaging. Its 144 synthetic memory layouts
must show that actual reanimation state controls eligibility independently of
the bytes that the old 0.3.1 build read. This is an ABI regression test, not an
engine simulation or proof of visible arrow placement.

Repeat the threshold tests with a controlled target whose health values can be
read before and after each shot. Do not call the build stable until the matrix is
confirmed in game and conflicts with other projectile-hooking DLLs are reviewed.
