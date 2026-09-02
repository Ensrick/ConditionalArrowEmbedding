# In-game test plan

Use a disposable save and test with debug logging enabled.

1. Shoot a healthy actor in the head without killing it: damage applies and the
   arrow does not remain embedded.
2. Kill an actor with a headshot: damage applies and vanilla may retain the arrow.
3. Shoot an actor above and exactly at 50% post-hit health in the torso: damage
   applies and the arrow does not remain embedded.
4. Shoot an actor below 50% post-hit health in the torso: vanilla embedding is
   preserved.
5. Kill an actor with a body shot: vanilla embedding is preserved.
6. Block an arrow and test an existing ricochet effect: vanilla behavior remains.
7. Test the player as target, humanoids, creatures with unusual skeletons, and
   arrows from mod-added ammunition.
8. Repeat representative cases with crossbow bolts; confirm spells and other
   non-ballistic projectiles retain vanilla behavior.
9. Inspect `ConditionalArrowEmbedding.log` and the crash logger after the matrix.

Repeat the threshold tests with a controlled target whose health values can be
read before and after each shot. Do not call the build stable until the matrix is
confirmed in game and conflicts with other projectile-hooking DLLs are reviewed.
