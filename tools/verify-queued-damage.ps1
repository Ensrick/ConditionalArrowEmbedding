# Dot-sourced by verify-runtime-hook.ps1 after its validated PE/Address Library
# inputs and assertion helpers are initialized. This performs read-only checks.

$processHitLibraryRva = Get-AddressLibraryRva $libraryBytes 38586 $offsetCount
$queueRunnerRva = Get-AddressLibraryRva $libraryBytes 36991 $offsetCount
if ($processHitLibraryRva -ne 0x6CA640 -or $processHitLibraryRva -ne $actorProcessHitRva -or
    $queueRunnerRva -ne 0x669920) { throw 'Queued damage Address Library mappings changed' }
Assert-BytesAtRva $exeBytes $layout 0x6CA6D6 ([byte[]](0x84,0xC0,0x74,0x17)) 'ProcessHit defer-condition branch'
Assert-BytesAtRva $exeBytes $layout 0x6CA6DA `
    ([byte[]](0x4D,0x8B,0xC5,0x49,0x8B,0xD7,0x48,0x8B,0x0D,0x69,0x7F,0xB7,0x02)) `
    'queue producer argument setup'
$queueProducerRva = Resolve-Rel32TargetRva $exeBytes $layout 0x6CA6E7 ([byte[]](0xE8)) 5 'queue producer call'
$queuedExitRva = Resolve-Rel32TargetRva $exeBytes $layout 0x6CA6EC ([byte[]](0xE9)) 5 'queued return bypasses damage body'
if ($queueProducerRva -ne 0x667D80 -or $queuedExitRva -ne 0x6CB7C2) { throw 'ProcessHit queue branch changed' }
Assert-BytesAtRva $exeBytes $layout 0x667DC8 `
    ([byte[]](0xC7,0x44,0x24,0x30,0x10,0x00,0x00,0x00,0x48,0x89,0x54,0x24,0x38)) `
    'queue command type 0x10 and retained actor field'
Assert-BytesAtRva $exeBytes $layout 0x667E04 ([byte[]](0xBA,0x90,0x00,0x00,0x00)) 'allocate 0x90 byte HitData'
$hitCopyRva = Resolve-Rel32TargetRva $exeBytes $layout 0x667E28 ([byte[]](0xE8)) 5 'queued HitData copy constructor'
if ($hitCopyRva -ne 0x7EF890) { throw 'Queued HitData copy target changed' }
Assert-BytesAtRva $exeBytes $layout 0x667E2E `
    ([byte[]](0x48,0x89,0x44,0x24,0x40,0x48,0x8D,0x54,0x24,0x30,0x48,0x8B,0xCE)) `
    'copied HitData stored in command +0x10 before enqueue'
$queueEnqueueRva = Resolve-Rel32TargetRva $exeBytes $layout 0x667E3B ([byte[]](0xE8)) 5 'engine command enqueue'
if ($queueEnqueueRva -ne 0x66BFC0) { throw 'Engine enqueue target changed' }
Assert-BytesAtRva $exeBytes $layout 0x66997C `
    ([byte[]](0x8B,0x01,0x83,0xC0,0xFE,0x83,0xF8,0x60)) 'queue runner command-minus-two index'
Assert-BytesAtRva $exeBytes $layout 0x669993 `
    ([byte[]](0x8B,0x84,0x81,0xFC,0xBD,0x66,0x00)) 'queue runner command switch table'
$hitCommandTarget = Read-UInt32 $exeBytes (Convert-RvaToFileOffset $layout (0x66BDFC + ((0x10 - 2) * 4)))
if ($hitCommandTarget -ne 0x66A343) { throw 'Command 0x10 does not select queued actor-hit consumer' }
Assert-BytesAtRva $exeBytes $layout 0x66A343 `
    ([byte[]](0x48,0x8B,0x77,0x08,0x48,0x8B,0x5F,0x10,0x48,0x85,0xF6,0x74,0x30,
              0x48,0x83,0x7E,0x68,0x00,0x74,0x0B,0x48,0x8B,0xD3,0x48,0x8B,0xCE)) `
    'queued actor and copied HitData arguments, with canceled-actor skip'
$queuedProcessHitRva = Resolve-Rel32TargetRva $exeBytes $layout 0x66A35D ([byte[]](0xE8)) 5 'queued actual ProcessHit'
if ($queuedProcessHitRva -ne $actorProcessHitRva) { throw 'Queued hit does not call reviewed ProcessHit' }
Assert-BytesAtRva $exeBytes $layout 0x66A362 ([byte[]](0x48,0x8D,0x4E,0x20)) 'actor release occurs after completion hook'

# Queue-vs-update ordering is not assumed. Pending visuals take the normal
# ProcessImpacts false-return exit without cleanup. No damage is applied here.
if ($processImpactsRva -ne 0x7DDEC0) { throw 'Reviewed arrow visual consumer changed' }
Assert-BytesAtRva $exeBytes $layout $processImpactsRva `
    ([byte[]](0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x81,0xD4,0x01,0x00,0x00)) 'arrow visual consumer entry'
Assert-BytesAtRva $exeBytes $layout 0x7FA8A3 `
    ([byte[]](0xFF,0x90,0x60,0x05,0x00,0x00,0x84,0xC0,0x74,0x33)) `
    'normal projectile update: false ProcessImpacts branches directly to exit'
Assert-BytesAtRva $exeBytes $layout 0x7FA8E0 `
    ([byte[]](0x4C,0x8D,0x9C,0x24,0x80,0x00,0x00,0x00,0x49,0x8B,0x5B,0x18,0x49,0x8B,0x73,0x28,
              0x0F,0x28,0x74,0x24,0x70,0x0F,0x28,0x7C,0x24,0x60,0x49,0x8B,0xE3,0x5F,0xC3)) `
    'normal projectile update no-cleanup return'
Assert-BytesAtRva $exeBytes $layout 0x8029B9 `
    ([byte[]](0x41,0x8B,0x86,0xD4,0x01,0x00,0x00,0xC1,0xE8,0x0F,0xA8,0x01)) `
    'special destroy-after-hit path checks projectile flag bit 15'
Assert-BytesAtRva $exeBytes $layout 0x8029DA `
    ([byte[]](0xFF,0x90,0x60,0x05,0x00,0x00,0x49,0x8B,0xCE,0xE8)) `
    'destroy-after-hit path ignores visual return, so must be excluded'

# Regression 0.3.3: bit 15 is set for ordinary non-hitscan arrows too. Prove
# its initialization through ArrowProjectile::Handle3DLoaded, then prove the
# special ignored-return caller is inside a function requiring an explosion.
$arrowInitVa = Read-UInt64 $exeBytes (Convert-RvaToFileOffset $layout ($arrowVtableRva + (0xC0 * 8)))
if ($arrowInitVa - $layout.ImageBase -ne 0x7DE270) { throw 'Arrow Handle3DLoaded vtable target changed' }
$missileInitRva = Resolve-Rel32TargetRva $exeBytes $layout 0x7DE287 ([byte[]](0xE8)) 5 'arrow missile initialization'
if ($missileInitRva -ne 0x7F5090) { throw 'Arrow missile initialization target changed' }
Assert-BytesAtRva $exeBytes $layout 0x7F50C1 `
    ([byte[]](0x41,0x0F,0xB6,0x90,0x80,0x00,0x00,0x00,0x80,0xE2,0x01)) 'base hitscan flag read into dl'
Assert-BytesAtRva $exeBytes $layout 0x7F5187 `
    ([byte[]](0x8B,0xC1,0x0F,0xBA,0xE9,0x0F,0x0F,0xBA,0xF0,0x0F,0x84,0xD2,0x0F,0x45,0xC8,
              0x8B,0xC1,0x0F,0xBA,0xE9,0x0D,0x0F,0xBA,0xF0,0x0D,0x84,0xD2,0x0F,0x44,0xC8,
              0x89,0x8F,0xD4,0x01,0x00,0x00)) 'bit 15 is inverse hitscan; bit 13 mirrors hitscan'
Assert-BytesAtRva $exeBytes $layout 0x8025B3 `
    ([byte[]](0x45,0x33,0xFF,0x4C,0x8B,0x69,0x60,0x4D,0x85,0xED,0x74,0x6F,
              0x4C,0x39,0xB9,0x58,0x01,0x00,0x00,0x74,0x66)) 'special lifecycle requires runtime explosion pointer'
Assert-BytesAtRva $exeBytes $layout 0x80262E `
    ([byte[]](0x49,0x8B,0xC7,0x4C,0x8D,0x9C,0x24,0x30,0x01,0x00,0x00)) 'no explosion returns before ignored-return visual call'
Assert-BytesAtRva $exeBytes $layout 0x802654 ([byte[]](0x5D,0xC3)) 'special lifecycle early-return epilogue'

# HandleHits marks damage submission handled. Both normal actor and fallback
# damage paths in Projectile::ProcessImpacts skip already-handled ImpactData.
# Our visual gate waits for marker=1 even if the queue completed first, and
# never writes this marker, so resuming visuals cannot replay native damage.
Assert-BytesAtRva $exeBytes $layout 0x800D79 `
    ([byte[]](0x4D,0x85,0xED,0x74,0x05,0x41,0xC6,0x45,0x48,0x01)) 'HandleHits submission marker write'
Assert-BytesAtRva $exeBytes $layout 0x7FAA6B `
    ([byte[]](0x41,0x80,0x7D,0x48,0x00,0x0F,0x85)) 'normal visual damage replay guard'
$normalDamageSkipRva = Resolve-Rel32TargetRva $exeBytes $layout 0x7FAA70 ([byte[]](0x0F,0x85)) 6 'handled normal damage skip'
if ($normalDamageSkipRva -ne 0x7FAC54) { throw 'Handled normal damage skip changed' }
Assert-BytesAtRva $exeBytes $layout 0x7FAB50 `
    ([byte[]](0x41,0x80,0x7D,0x48,0x00,0x75,0x33)) 'fallback visual damage replay guard'

if (-not $RuntimeOnly) {
    $queuedHookCode = Remove-CppComments (Get-Content -LiteralPath $HookSource -Raw)
    foreach ($required in @('QueueSubmissionCallOffset\s*=\s*0xA7',
        'QueuedActorHitCallOffset\s*=\s*0xA3D', 'ProcessImpactsVtableIndex\s*=\s*0xAC',
        'TryPrepareHit\s*\(', 'RunDamageDispatch\s*\(', 'DamageDispatchScope::MarkDeferred\s*\(',
        'g_deferredImpacts\.Register\s*\(', 'g_deferredImpacts\.Complete\s*\(',
        'g_deferredImpacts\.Consume\s*\(', 'VisualGateAction::Wait',
        'HasUnsupportedDeferredLifecycle\(a_projectile\)', 'before\.reset\s*\(',
        'impact\s*&&\s*impact->unk48\s*==\s*1',
        'arrowVtable\.write_vfunc\s*\(\s*ProcessImpactsVtableIndex')) {
        if ($queuedHookCode -notmatch $required) { throw "Missing queued-damage production contract: $required" }
    }
    if ($queuedHookCode -notmatch '(?s)RegisterQueuedImpact\(a_target, a_hitData\).*?Func\(a_queue, a_target, a_hitData\)' -or
        $queuedHookCode -notmatch '(?s)VisualGateAction::Wait\).*?return false;') {
        throw 'Queued registration must precede submission and pending visuals must not call original consumer'
    }
    if ($queuedHookCode -match 'unk48\s*=(?!=)') { throw 'Hooks must not clear or rewrite native handled marker' }
    $lifecycleCode = Remove-CppComments (Get-Content -LiteralPath (Join-Path $repoRoot 'src/ProjectileLifecycle.h') -Raw)
    if ($lifecycleCode -notmatch 'data\.explosion\s*!=\s*nullptr\s*\|\|\s*data\.flags\.any\(RE::Projectile::Flags::kChainShatter\)' -or
        $lifecycleCode -match 'kDestroyAfterHit') {
        throw 'Ordinary non-hitscan arrows must not be excluded by bit 15; special explosion/chain lifecycles remain excluded'
    }
}

$queuedDamageProof = [ordered]@{
    producer = '38586 + 0xA7 -> 0x667D80'
    commandType = '0x10'
    hitDataCopied = $true
    queuedCompletion = '36991 + 0xA3D -> 0x6CA640'
    earlyReturnSkipsDamage = $true
    visualGate = '209891 vtable[0xAC] -> 0x7DDEC0'
    normalFalseReturnExitsWithoutCleanup = $true
    nativeHandledMarkerPreventsDamageReplay = $true
    ordinaryDestroyAfterHitFlagAllowed = $true
    explosionAndChainShatterExcluded = $true
    arrowInitializationProvesBit15IsInverseHitscan = $true
    queueDrainOrderingAssumed = $false
    runtimeGameplayVerified = $false
}
