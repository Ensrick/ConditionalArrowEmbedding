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

## SKSE and Address Library

SKSE and Address Library are runtime requirements and are not redistributed by
this project.
