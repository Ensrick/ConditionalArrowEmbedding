#Requires -Version 7.0

[CmdletBinding()]
param(
    [string]$Version = "0.3.1",
    [string]$StageRoot = "",
    [string]$OutputDirectory = "",
    [switch]$AllowDirty
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($StageRoot)) { $StageRoot = Join-Path $repoRoot "build/release/stage" }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot "build/release/packages" }
$StageRoot = [System.IO.Path]::GetFullPath($StageRoot)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $StageRoot -PathType Container)) { throw "Staged build is missing" }

function Get-GitValue([string[]]$Arguments) {
    $value = & git -C $repoRoot @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return (($value | Out-String).Trim())
}
function Get-RelativePath([string]$Base, [string]$Path) {
    return [System.IO.Path]::GetRelativePath($Base, $Path).Replace('\', '/')
}
function Write-Utf8([string]$Path, [string]$Content) {
    $normalized = $Content.Replace("`r`n", "`n").Replace("`r", "`n")
    if (-not $normalized.EndsWith("`n")) { $normalized += "`n" }
    [System.IO.File]::WriteAllText($Path, $normalized, [System.Text.UTF8Encoding]::new($false))
}
function New-DeterministicZip([string]$Root, [string]$Destination, [DateTimeOffset]$Timestamp) {
    Add-Type -AssemblyName System.IO.Compression
    $stream = [System.IO.File]::Open($Destination, 'Create', 'ReadWrite', 'None')
    try {
        $archive = [System.IO.Compression.ZipArchive]::new($stream, 'Create', $false)
        try {
            Get-ChildItem -LiteralPath $Root -File -Recurse |
                Sort-Object { Get-RelativePath $Root $_.FullName } |
                ForEach-Object {
                    $relative = Get-RelativePath $Root $_.FullName
                    $entry = $archive.CreateEntry($relative, 'Optimal')
                    $entry.LastWriteTime = $Timestamp
                    $entry.ExternalAttributes = 0
                    $input = [System.IO.File]::OpenRead($_.FullName)
                    $output = $entry.Open()
                    try { $input.CopyTo($output) } finally { $output.Dispose(); $input.Dispose() }
                }
        } finally { $archive.Dispose() }
    } finally { $stream.Dispose() }
}

$versionHeader = Get-Content -LiteralPath (Join-Path $repoRoot "src/Version.h") -Raw
if ($versionHeader -notmatch 'Semantic\s*=\s*"([^\"]+)"' -or $Matches[1] -ne $Version) {
    throw "Package version does not match src/Version.h"
}
$commit = Get-GitValue @('rev-parse', 'HEAD')
$status = Get-GitValue @('status', '--porcelain=v1', '--untracked-files=normal')
$dirty = [string]::IsNullOrWhiteSpace($commit) -or -not [string]::IsNullOrWhiteSpace($status)
if ($dirty -and -not $AllowDirty) { throw "Release packaging requires a clean commit" }
if ([string]::IsNullOrWhiteSpace($commit)) { $commit = 'UNCOMMITTED' }
$epochText = Get-GitValue @('show', '-s', '--format=%ct', 'HEAD')
$epoch = 0L
if (-not [long]::TryParse($epochText, [ref]$epoch)) { $epoch = 0L }
$timestamp = [DateTimeOffset]::FromUnixTimeSeconds($epoch)
if ($timestamp.Year -lt 1980) { $timestamp = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero) }

$tempBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$workRoot = Join-Path $tempBase ("ConditionalArrowEmbedding-package-" + [guid]::NewGuid().ToString('N'))
$payloadRoot = Join-Path $workRoot 'payload'
$sourceRoot = Join-Path $workRoot 'commonlib-source'
New-Item -ItemType Directory -Path $payloadRoot, $sourceRoot -Force | Out-Null
try {
    Get-ChildItem -LiteralPath $StageRoot -File -Recurse | ForEach-Object {
        $relative = Get-RelativePath $StageRoot $_.FullName
        $destination = Join-Path $payloadRoot $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination
    }
    $forbiddenPayload = Get-ChildItem -LiteralPath $payloadRoot -File -Recurse |
        Where-Object Extension -in @('.pdb', '.lib', '.exp', '.obj', '.log', '.sav')
    if ($forbiddenPayload) {
        throw "Forbidden development or user-data file in mod payload: $($forbiddenPayload.FullName -join ', ')"
    }
    foreach ($license in @(
        @{ Source = 'extern/CommonLibSSE-NG/COPYING'; Destination = 'licenses/CommonLibSSE-NG/COPYING' },
        @{ Source = 'extern/CommonLibSSE-NG/EXCEPTIONS.md'; Destination = 'licenses/CommonLibSSE-NG/EXCEPTIONS.md' },
        @{ Source = 'extern/CommonLibSSE-NG/licenses/LICENSE-MIT'; Destination = 'licenses/CommonLibSSE-NG/LICENSE-MIT' }
    )) {
        $source = Join-Path $repoRoot $license.Source
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing license: $($license.Source)" }
        $destination = Join-Path $payloadRoot $license.Destination
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }
    $dependencyRoot = Join-Path $repoRoot 'build/release/vcpkg_installed/x64-windows-static-md/share'
    $manifest = Get-Content -LiteralPath (Join-Path $repoRoot 'vcpkg.json') -Raw | ConvertFrom-Json
    foreach ($dependency in $manifest.dependencies) {
        $name = if ($dependency -is [string]) { $dependency } else { [string]$dependency.name }
        $source = Join-Path $dependencyRoot "$name/copyright"
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing dependency license: $name" }
        $destination = Join-Path $payloadRoot "licenses/vcpkg/$name/copyright"
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }
    $buildInfo = [ordered]@{
        schemaVersion = 1
        name = 'Conditional Arrow Embedding'
        version = $Version
        runtime = [ordered]@{ skyrim = '1.7.104.0'; skse = '2.3.1' }
        source = [ordered]@{ repository = 'https://github.com/Ensrick/ConditionalArrowEmbedding'; commit = $commit; dirty = $dirty }
        commonLibSseNgCommit = 'a9d7d4523d5e1abc8b296bd99683b7df11df652f'
    }
    Write-Utf8 (Join-Path $payloadRoot 'BUILD-INFO.json') ($buildInfo | ConvertTo-Json -Depth 6)
    $files = Get-ChildItem -LiteralPath $payloadRoot -File -Recurse |
        Where-Object Name -ne 'MANIFEST.sha256' |
        Sort-Object { Get-RelativePath $payloadRoot $_.FullName }
    $hashes = $files | ForEach-Object {
        "$( (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() )  $(Get-RelativePath $payloadRoot $_.FullName)"
    }
    Write-Utf8 (Join-Path $payloadRoot 'MANIFEST.sha256') ($hashes -join "`n")

    $submoduleRoot = Join-Path $repoRoot 'extern/CommonLibSSE-NG'
    $tracked = & git -C $submoduleRoot ls-files
    if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate CommonLibSSE-NG source' }
    foreach ($relative in $tracked) {
        $source = Join-Path $submoduleRoot $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        $destination = Join-Path $sourceRoot $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }
    Write-Utf8 (Join-Path $sourceRoot 'SOURCE-PROVENANCE.json') (([ordered]@{
        repository = 'https://github.com/Ensrick/CommonLibSSE-NG'
        commit = 'a9d7d4523d5e1abc8b296bd99683b7df11df652f'
        upstreamCommit = '8b032fa992750d654d6d38a33731714d8b86be1f'
        bundledFor = "Conditional Arrow Embedding $Version"
    }) | ConvertTo-Json)

    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $suffix = if ($dirty) { '-dirty' } else { '' }
    $archive = Join-Path $OutputDirectory "ConditionalArrowEmbedding-$Version-Skyrim-1.7.104-win64$suffix.zip"
    $sourceArchive = Join-Path $OutputDirectory "ConditionalArrowEmbedding-$Version-CommonLibSSE-NG-source$suffix.zip"
    New-DeterministicZip $payloadRoot $archive $timestamp
    New-DeterministicZip $sourceRoot $sourceArchive $timestamp
    $archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Utf8 "$archive.sha256" "$archiveHash  $(Split-Path -Leaf $archive)"
    [ordered]@{
        archive = $archive
        sha256 = $archiveHash
        correspondingSourceArchive = $sourceArchive
        releaseEligible = (-not $dirty)
    } | ConvertTo-Json
} finally {
    $resolved = [System.IO.Path]::GetFullPath($workRoot)
    if ($resolved.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved).StartsWith('ConditionalArrowEmbedding-package-', [StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
