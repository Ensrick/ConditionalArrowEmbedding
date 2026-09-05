# Third-party notices

## CommonLibSSE-NG

This project statically links the Ensrick no-modal-errors fork of
CommonLibSSE-NG at commit `a9d7d4523d5e1abc8b296bd99683b7df11df652f`,
based directly on upstream v7.0.0 commit
`8b032fa992750d654d6d38a33731714d8b86be1f`.

CommonLibSSE-NG is licensed under GPL-3.0-or-later with its Modding Exception.
The controlling `COPYING` and `EXCEPTIONS.md` files are included in packaged
artifacts, and the exact corresponding source is published with binary
releases.

## Archery Locational Damage

The MIT-licensed source of tossaponk's Archery Locational Damage was inspected
as prior art for Skyrim's projectile-impact flow, particularly the safe use of
the engine's `Bounce` impact result after a hit has been processed. Conditional
Arrow Embedding has its own narrower policy, configuration, callsite validation,
tests, and implementation.

Repository: https://github.com/tossaponk/ArcheryLocationalDamage

Reviewed commit: `1ae6e4e6` (2.1.6)

## Core Impact Framework and Ricochet Framework

Seb263's GPL-3.0-licensed Core Impact Framework and Ricochet Framework sources
were inspected as behavioral prior art for the public projectile collision
entry points. This project's implementation and executable verifier were
written independently; no GPL source is copied or linked.

Repositories: https://github.com/Seb263/SkyrimSE_CoreImpactFramework and
https://github.com/Seb263/SkyrimSE_RicochetFramework

Reviewed commits: `fc502165db8c4870b52c452596ae82664d949768` and
`2979bb6d7d66f76224dcf4f6f514782d16d613d8`.

## SKSE and Address Library

SKSE and Address Library are runtime requirements and are not redistributed by
this project.
