#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$SkyrimExe = "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\SkyrimSE.exe",
    [string]$AddressLibrary = "C:\Users\danjo\source\repos\mo2-instances\skyrim-se\mods\Address Library\SKSE\Plugins\versionlib-1-7-104-0.bin",
    [string]$HookSource = "",
    [switch]$RuntimeOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($HookSource)) {
    $HookSource = Join-Path $repoRoot "src/Hooks.cpp"
} elseif (-not [System.IO.Path]::IsPathRooted($HookSource)) {
    $HookSource = Join-Path $repoRoot $HookSource
}

$requiredFiles = @($SkyrimExe, $AddressLibrary)
if (-not $RuntimeOnly) { $requiredFiles += $HookSource }
foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required runtime-audit input is missing: $file"
    }
}

function Read-UInt16([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}
function Read-UInt32([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}
function Read-UInt64([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt64($Bytes, $Offset)
}
function Read-Int32([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToInt32($Bytes, $Offset)
}

function Get-PeLayout([byte[]]$Bytes) {
    $pe = [BitConverter]::ToInt32($Bytes, 0x3C)
    if ($pe -lt 0 -or $pe + 48 -ge $Bytes.Length -or
        $Bytes[$pe] -ne 0x50 -or $Bytes[$pe + 1] -ne 0x45) {
        throw "SkyrimSE.exe has an invalid PE header"
    }
    $optionalHeader = $pe + 24
    if ((Read-UInt16 $Bytes $optionalHeader) -ne 0x20B) {
        throw "SkyrimSE.exe is not a PE32+ image"
    }
    $sectionCount = Read-UInt16 $Bytes ($pe + 6)
    $optionalHeaderSize = Read-UInt16 $Bytes ($pe + 20)
    $sectionTable = $optionalHeader + $optionalHeaderSize
    $sections = @()
    for ($index = 0; $index -lt $sectionCount; ++$index) {
        $section = $sectionTable + ($index * 40)
        $virtualSize = Read-UInt32 $Bytes ($section + 8)
        $virtualAddress = Read-UInt32 $Bytes ($section + 12)
        $rawSize = Read-UInt32 $Bytes ($section + 16)
        $rawAddress = Read-UInt32 $Bytes ($section + 20)
        $sections += [pscustomobject]@{
            VirtualAddress = [uint32]$virtualAddress
            Span = [uint32][Math]::Max($virtualSize, $rawSize)
            RawAddress = [uint32]$rawAddress
        }
    }
    return [pscustomobject]@{
        ImageBase = Read-UInt64 $Bytes ($optionalHeader + 24)
        Sections = @($sections)
    }
}

function Convert-RvaToFileOffset([object]$Layout, [uint32]$Rva) {
    foreach ($section in $Layout.Sections) {
        if ($Rva -ge $section.VirtualAddress -and
            [uint64]$Rva -lt ([uint64]$section.VirtualAddress + $section.Span)) {
            return [int]($section.RawAddress + ($Rva - $section.VirtualAddress))
        }
    }
    throw ('RVA 0x{0:X} is not contained in a PE section' -f $Rva)
}

function Assert-BytesAtRva(
    [byte[]]$Bytes,
    [object]$Layout,
    [uint32]$Rva,
    [byte[]]$Expected,
    [string]$Description
) {
    $offset = Convert-RvaToFileOffset $Layout $Rva
    for ($index = 0; $index -lt $Expected.Length; ++$index) {
        if ($Bytes[$offset + $index] -ne $Expected[$index]) {
            throw ('{0} mismatch at RVA 0x{1:X} (+0x{2:X}); expected 0x{3:X2}, got 0x{4:X2}' -f
                $Description, $Rva, $index, $Expected[$index], $Bytes[$offset + $index])
        }
    }
}

function Resolve-Rel32TargetRva(
    [byte[]]$Bytes,
    [object]$Layout,
    [uint32]$InstructionRva,
    [byte[]]$Opcode,
    [int]$InstructionLength,
    [string]$Description
) {
    Assert-BytesAtRva $Bytes $Layout $InstructionRva $Opcode $Description
    $instructionOffset = Convert-RvaToFileOffset $Layout $InstructionRva
    $displacement = Read-Int32 $Bytes ($instructionOffset + $Opcode.Length)
    $target = [int64]$InstructionRva + $InstructionLength + $displacement
    if ($target -lt 0 -or $target -gt [uint32]::MaxValue) {
        throw "$Description resolves outside the image RVA range"
    }
    return [uint32]$target
}

function Get-AddressLibraryRva([byte[]]$Bytes, [int]$AddressId, [int]$OffsetCount) {
    if ($AddressId -lt 0 -or $AddressId -ge $OffsetCount) {
        throw "Address Library does not contain ID $AddressId"
    }
    return Read-UInt32 $Bytes (96 + ($AddressId * 4))
}

function Remove-CppComments([string]$Text) {
    $withoutBlocks = [regex]::Replace($Text, '(?s)/\*.*?\*/', '')
    return [regex]::Replace($withoutBlocks, '(?m)//.*$', '')
}

$version = (Get-Item -LiteralPath $SkyrimExe).VersionInfo.FileVersion
if ($version -ne '1.7.104.0') {
    throw "Unsupported Skyrim executable version: $version"
}

$libraryBytes = [IO.File]::ReadAllBytes($AddressLibrary)
if ($libraryBytes.Length -lt 96) { throw "Address Library file is truncated" }
$format = [BitConverter]::ToInt32($libraryBytes, 0)
$libraryVersion = @(4, 8, 12, 16) | ForEach-Object { [BitConverter]::ToInt32($libraryBytes, $_) }
$pointerSize = [BitConverter]::ToInt32($libraryBytes, 84)
$offsetCount = [BitConverter]::ToInt32($libraryBytes, 92)
if ($format -ne 5 -or ($libraryVersion -join '.') -ne '1.7.104.0' -or $pointerSize -ne 8) {
    throw "Address Library header is not format 5 for Skyrim 1.7.104.0 x64"
}

# These three IDs establish the semantic call chain, rather than merely proving
# that a convenient E8 instruction exists:
#
#   ArrowProjectile::HandleHits vtable[0xBE] (ID 44204)
#     owner-resolved actor branch -> projectile actor-hit dispatcher
#       -> ID 38627 -> Actor::ProcessHit at +0x4A8
#
# ID 44204 + 0x3AA reaches the same Actor::ProcessHit routine only from the
# source-null fallback branch. It is deliberately audited below as a rejected
# regression candidate.
$actorHitDispatchId = 38627
$actorHitCallOffset = 0x4A8
$arrowHandleHitsId = 44204
$deprecatedFallbackOffset = 0x3AA
$arrowVtableId = 209891
$arrowHandleHitsIndex = 0xBE
$arrowProcessImpactsIndex = 0xAC

$actorHitDispatchRva = Get-AddressLibraryRva $libraryBytes $actorHitDispatchId $offsetCount
$arrowHandleHitsRva = Get-AddressLibraryRva $libraryBytes $arrowHandleHitsId $offsetCount
$arrowVtableRva = Get-AddressLibraryRva $libraryBytes $arrowVtableId $offsetCount
if ($actorHitDispatchRva -ne 0x6CD2F0 -or
    $arrowHandleHitsRva -ne 0x8009C0 -or
    $arrowVtableRva -ne 0x193C248) {
    throw ('Reviewed Address Library RVAs changed: ID {0}=0x{1:X}, ID {2}=0x{3:X}, ID {4}=0x{5:X}' -f
        $actorHitDispatchId, $actorHitDispatchRva,
        $arrowHandleHitsId, $arrowHandleHitsRva,
        $arrowVtableId, $arrowVtableRva)
}

$exeBytes = [IO.File]::ReadAllBytes($SkyrimExe)
$layout = Get-PeLayout $exeBytes

# Prove that ID 44204 is ArrowProjectile::HandleHits, not ProcessImpacts.
$handleHitsEntryRva = [uint32]($arrowVtableRva + ($arrowHandleHitsIndex * 8))
$handleHitsEntryOffset = Convert-RvaToFileOffset $layout $handleHitsEntryRva
$handleHitsVa = Read-UInt64 $exeBytes $handleHitsEntryOffset
$handleHitsFromVtableRva = [uint32]($handleHitsVa - $layout.ImageBase)
if ($handleHitsFromVtableRva -ne $arrowHandleHitsRva) {
    throw ('ArrowProjectile vtable[0xBE] resolves to RVA 0x{0:X}, not Address Library ID {1}' -f
        $handleHitsFromVtableRva, $arrowHandleHitsId)
}
$processImpactsEntryRva = [uint32]($arrowVtableRva + ($arrowProcessImpactsIndex * 8))
$processImpactsEntryOffset = Convert-RvaToFileOffset $layout $processImpactsEntryRva
$processImpactsVa = Read-UInt64 $exeBytes $processImpactsEntryOffset
$processImpactsRva = [uint32]($processImpactsVa - $layout.ImageBase)
if ($processImpactsRva -eq $arrowHandleHitsRva) {
    throw "ArrowProjectile ProcessImpacts and HandleHits unexpectedly resolve to the same function"
}

# Prove that the value mutated after damage is consumed by the later visual
# impact phase. ArrowProjectile::ProcessImpacts compares the AE missile runtime
# result at +0x1E0 with Stick (4), conditionally runs its embed path, and then
# tail-jumps to MissileProjectile::ProcessImpacts, which switches on the same
# field for Bounce/Impale/Stick handling.
$arrowStickReadRva = [uint32]($processImpactsRva + 0x31)
Assert-BytesAtRva $exeBytes $layout $arrowStickReadRva `
    ([byte[]](0x83, 0xBB, 0xE0, 0x01, 0x00, 0x00, 0x04, 0x75, 0x1C)) `
    "ArrowProjectile ProcessImpacts stick-result consumer"
$missileProcessImpactsRva = Resolve-Rel32TargetRva $exeBytes $layout `
    ([uint32]($processImpactsRva + 0x71)) ([byte[]](0xE9)) 5 `
    "ArrowProjectile ProcessImpacts tail call"
Assert-BytesAtRva $exeBytes $layout ([uint32]($missileProcessImpactsRva + 0xB1)) `
    ([byte[]](0x8B, 0x8B, 0xE0, 0x01, 0x00, 0x00,
              0x83, 0xE9, 0x01, 0x74, 0x63)) `
    "MissileProjectile ProcessImpacts result switch"

# Prove why the legacy site misses ordinary arrows. HandleHits reads the
# projectile shooter handle, resolves it, tests the resolved Character in r14,
# and jumps to the +0x388 fallback only when that source is null. The ordinary
# owner-resolved branch calls a separate dispatcher and skips the fallback.
Assert-BytesAtRva $exeBytes $layout ([uint32]($arrowHandleHitsRva + 0x283)) `
    ([byte[]](0x8B, 0x87, 0x28, 0x01, 0x00, 0x00)) `
    "HandleHits shooter-handle load"
Assert-BytesAtRva $exeBytes $layout ([uint32]($arrowHandleHitsRva + 0x2CA)) `
    ([byte[]](0x4D, 0x85, 0xF6)) `
    "HandleHits resolved-source test"
$fallbackBranchRva = [uint32]($arrowHandleHitsRva + 0x2CD)
$fallbackTargetRva = Resolve-Rel32TargetRva $exeBytes $layout $fallbackBranchRva `
    ([byte[]](0x0F, 0x84)) 6 "HandleHits source-null branch"
if ($fallbackTargetRva -ne [uint32]($arrowHandleHitsRva + 0x388)) {
    throw ('HandleHits source-null branch resolves to unexpected RVA 0x{0:X}' -f $fallbackTargetRva)
}

$normalOwnerCallRva = [uint32]($arrowHandleHitsRva + 0x2D9)
$normalOwnerDispatcherRva = Resolve-Rel32TargetRva $exeBytes $layout $normalOwnerCallRva `
    ([byte[]](0xE8)) 5 "HandleHits owner-resolved actor dispatcher call"
$normalOwnerSkipRva = Resolve-Rel32TargetRva $exeBytes $layout ([uint32]($arrowHandleHitsRva + 0x2DE)) `
    ([byte[]](0xE9)) 5 "HandleHits owner-resolved fallback skip"
if ($normalOwnerSkipRva -ne [uint32]($arrowHandleHitsRva + 0x3B9)) {
    throw ('HandleHits owner-resolved branch does not skip the source-null fallback; target RVA 0x{0:X}' -f
        $normalOwnerSkipRva)
}

# The owner-resolved projectile dispatcher must in turn enter ID 38627. This is
# the normal ballistic-arrow route that the deprecated +0x3AA patch bypassed.
Assert-BytesAtRva $exeBytes $layout ([uint32]($normalOwnerDispatcherRva + 0x1C)) `
    ([byte[]](0x8B, 0x81, 0x28, 0x01, 0x00, 0x00)) `
    "owner-resolved dispatcher shooter-handle load"
$normalToActorHitRva = Resolve-Rel32TargetRva $exeBytes $layout `
    ([uint32]($normalOwnerDispatcherRva + 0x90)) ([byte[]](0xE8)) 5 `
    "owner-resolved dispatcher actor-hit call"
if ($normalToActorHitRva -ne $actorHitDispatchRva) {
    throw ('Owner-resolved arrow path calls RVA 0x{0:X}, not Address Library ID {1}' -f
        $normalToActorHitRva, $actorHitDispatchId)
}

# Validate the approved Actor::ProcessHit call in the normal actor-hit function.
$actorHitCallsiteRva = [uint32]($actorHitDispatchRva + $actorHitCallOffset)
Assert-BytesAtRva $exeBytes $layout ([uint32]($actorHitCallsiteRva - 18)) `
    ([byte[]](0x80, 0xBC, 0x24, 0x48, 0x01, 0x00, 0x00, 0x00,
              0x75, 0x29, 0x48, 0x8D, 0x54, 0x24, 0x50, 0x48, 0x8B, 0xCF)) `
    "normal actor-hit callsite prefix"
$actorProcessHitRva = Resolve-Rel32TargetRva $exeBytes $layout $actorHitCallsiteRva `
    ([byte[]](0xE8)) 5 "normal Actor::ProcessHit call"
Assert-BytesAtRva $exeBytes $layout ([uint32]($actorHitCallsiteRva + 5)) `
    ([byte[]](0xF3, 0x0F, 0x10, 0x8C, 0x24, 0xCC, 0x00, 0x00, 0x00,
              0x0F, 0x2F, 0xCE, 0x76, 0x0E)) `
    "normal actor-hit callsite suffix"

# Validate the rejected source-null fallback and prove that it reaches the same
# Actor::ProcessHit target. Matching targets alone was the original false proof:
# branch reachability and mutation phase are part of the contract too.
$deprecatedCallsiteRva = [uint32]($arrowHandleHitsRva + $deprecatedFallbackOffset)
Assert-BytesAtRva $exeBytes $layout ([uint32]($deprecatedCallsiteRva - 7)) `
    ([byte[]](0x48, 0x8D, 0x55, 0xC0, 0x48, 0x8B, 0xCE)) `
    "deprecated source-null callsite prefix"
$deprecatedTargetRva = Resolve-Rel32TargetRva $exeBytes $layout $deprecatedCallsiteRva `
    ([byte[]](0xE8)) 5 "deprecated source-null Actor::ProcessHit call"
Assert-BytesAtRva $exeBytes $layout ([uint32]($deprecatedCallsiteRva + 5)) `
    ([byte[]](0x90, 0x48, 0x8D, 0x4D, 0xC0)) `
    "deprecated source-null callsite suffix"
if ($deprecatedTargetRva -ne $actorProcessHitRva) {
    throw ('Reviewed actor-hit callsites no longer share one Actor::ProcessHit target: 0x{0:X} vs 0x{1:X}' -f
        $deprecatedTargetRva, $actorProcessHitRva)
}

# The collision routine also calls virtual slot 0xBD (AddImpact). Recording this
# phase boundary prevents a future audit from treating any post-collision E8 as
# sufficient merely because it has the right callee.
$addImpactCallRva = [uint32]($arrowHandleHitsRva + 0x34C)
Assert-BytesAtRva $exeBytes $layout $addImpactCallRva `
    ([byte[]](0x41, 0xFF, 0x92, 0xE8, 0x05, 0x00, 0x00)) `
    "HandleHits virtual AddImpact call"

# Source-to-binary contract: require the implementation to select the approved
# call and reject the old source-null fallback. Comments are stripped so they may
# still document the regression without satisfying or tripping this gate.
$sourceContractVerified = $false
if (-not $RuntimeOnly) {
    $hookCode = Remove-CppComments (Get-Content -LiteralPath $HookSource -Raw)
    $hasApprovedId =
        $hookCode -match 'REL::(?:ID|RelocationID)\s*\([^\)]*\b38627\b[^\)]*\)' -or
        (($hookCode -match '\bPhysicalHitDispatcherAddressId\s*=\s*38627\b') -and
         ($hookCode -match 'REL::ID\s*\(\s*PhysicalHitDispatcherAddressId\s*\)'))
    $hasApprovedOffset =
        $hookCode -match '\bphysicalHitDispatcher\s*\.\s*address\s*\(\s*\)\s*\+\s*0x4A8\b' -or
        (($hookCode -match '\bPhysicalHitCallOffset\s*=\s*0x4A8\b') -and
         ($hookCode -match '\bphysicalHitDispatcher\s*\.\s*address\s*\(\s*\)\s*\+\s*PhysicalHitCallOffset\b'))
    $hasDeprecatedId = $hookCode -match 'REL::(?:ID|RelocationID)\s*\([^\)]*\b44204\b[^\)]*\)'
    $hasDeprecatedOffset = $hookCode -match '\b0x3AA\b'
    if (-not $hasApprovedId -or -not $hasApprovedOffset) {
        throw "Hooks.cpp does not select the approved normal-arrow callsite (Address Library ID 38627 + 0x4A8)"
    }
    if ($hasDeprecatedId -and $hasDeprecatedOffset) {
        throw "Hooks.cpp still selects deprecated source-null callsite Address Library ID 44204 + 0x3AA"
    }
    if ($hookCode -notmatch '\bimpacts\s*\.\s*front\s*\(\s*\)' -or
        $hookCode -notmatch '\bimpact\s*->\s*unk48\s*!=\s*0\b') {
        throw "Hooks.cpp does not restrict mutation to the current unprocessed impact-list head"
    }
    if ($hookCode -match 'for\s*\([^\)]*:\s*[^\)]*\.impacts\s*\)') {
        throw "Hooks.cpp scans historical projectile impacts instead of using only the current head"
    }
    if ($hookCode -notmatch 'source\s*\?\s*source\s*->\s*As\s*<\s*RE::ArrowProjectile\s*>') {
        throw "Hooks.cpp does not retain and exact-cast the HitData projectile source"
    }
    if ($hookCode -match 'IsEmbedResult\s*\(\s*impactResultBefore\s*\)') {
        throw "Hooks.cpp incorrectly uses the per-impact result as the ProcessImpacts policy source"
    }
    if ($hookCode -notmatch 'kProcessedImpacts' -or $hookCode -notmatch 'kDestroyed') {
        throw "Hooks.cpp does not reject already-processed and destroyed projectiles"
    }
    $sourceContractVerified = $true
}

$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
$stream = [IO.File]::OpenRead($SkyrimExe)
try {
    $exeHash = [BitConverter]::ToString($hashAlgorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
} finally {
    $stream.Dispose()
    $hashAlgorithm.Dispose()
}

[ordered]@{
    executable = [IO.Path]::GetFullPath($SkyrimExe)
    executableVersion = $version
    executableSha256 = $exeHash
    addressLibrary = [IO.Path]::GetFullPath($AddressLibrary)
    addressLibraryFormat = $format
    hookSource = if ($RuntimeOnly) { $null } else { [IO.Path]::GetFullPath($HookSource) }
    sourceContractVerified = $sourceContractVerified
    arrowVtable = [ordered]@{
        addressLibraryId = $arrowVtableId
        rva = ('0x{0:X}' -f $arrowVtableRva)
        processImpactsIndex = ('0x{0:X}' -f $arrowProcessImpactsIndex)
        processImpactsRva = ('0x{0:X}' -f $processImpactsRva)
        handleHitsIndex = ('0x{0:X}' -f $arrowHandleHitsIndex)
        handleHitsAddressLibraryId = $arrowHandleHitsId
        handleHitsRva = ('0x{0:X}' -f $arrowHandleHitsRva)
    }
    approvedHook = [ordered]@{
        addressLibraryId = $actorHitDispatchId
        functionRva = ('0x{0:X}' -f $actorHitDispatchRva)
        callOffset = ('0x{0:X}' -f $actorHitCallOffset)
        callsiteRva = ('0x{0:X}' -f $actorHitCallsiteRva)
        actorProcessHitRva = ('0x{0:X}' -f $actorProcessHitRva)
        reachableFromOwnerResolvedArrowPath = $true
        mutationWindow = 'post-damage, before return to ArrowProjectile::HandleHits'
    }
    rejectedRegression = [ordered]@{
        addressLibraryId = $arrowHandleHitsId
        callOffset = ('0x{0:X}' -f $deprecatedFallbackOffset)
        callsiteRva = ('0x{0:X}' -f $deprecatedCallsiteRva)
        reason = 'source-null fallback; owner-resolved arrows branch around it'
        rejected = $true
    }
    collisionPhaseBoundary = [ordered]@{
        addImpactVtableIndex = '0xBD'
        observedCallsiteRva = ('0x{0:X}' -f $addImpactCallRva)
    }
    impactResultConsumer = [ordered]@{
        arrowProcessImpactsRva = ('0x{0:X}' -f $processImpactsRva)
        arrowStickReadRva = ('0x{0:X}' -f $arrowStickReadRva)
        missileProcessImpactsRva = ('0x{0:X}' -f $missileProcessImpactsRva)
        fieldOffset = '0x1E0'
        verified = $true
    }
    verified = $true
} | ConvertTo-Json -Depth 6
