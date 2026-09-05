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
   use the conditional rules.
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
    nonlethal test must produce `first conditional bounce applied`. With debug
    logging disabled, none of those categories may repeat during the session.

Repeat the threshold tests with a controlled target whose health values can be
read before and after each shot. Do not call the build stable until the matrix is
confirmed in game and conflicts with other projectile-hooking DLLs are reviewed.
