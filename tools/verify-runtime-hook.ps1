#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$SkyrimExe = "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\SkyrimSE.exe",
    [string]$AddressLibrary = "C:\Users\danjo\source\repos\mo2-instances\skyrim-se\mods\Address Library\SKSE\Plugins\versionlib-1-7-104-0.bin"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

foreach ($file in @($SkyrimExe, $AddressLibrary)) {
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
function Convert-RvaToFileOffset([byte[]]$Bytes, [uint32]$Rva) {
    $pe = [BitConverter]::ToInt32($Bytes, 0x3C)
    if ($pe -lt 0 -or $pe + 24 -ge $Bytes.Length -or
        $Bytes[$pe] -ne 0x50 -or $Bytes[$pe + 1] -ne 0x45) {
        throw "SkyrimSE.exe has an invalid PE header"
    }
    $sectionCount = Read-UInt16 $Bytes ($pe + 6)
    $optionalHeaderSize = Read-UInt16 $Bytes ($pe + 20)
    $sectionTable = $pe + 24 + $optionalHeaderSize
    for ($index = 0; $index -lt $sectionCount; ++$index) {
        $section = $sectionTable + ($index * 40)
        $virtualSize = Read-UInt32 $Bytes ($section + 8)
        $virtualAddress = Read-UInt32 $Bytes ($section + 12)
        $rawSize = Read-UInt32 $Bytes ($section + 16)
        $rawAddress = Read-UInt32 $Bytes ($section + 20)
        $span = [Math]::Max($virtualSize, $rawSize)
        if ($Rva -ge $virtualAddress -and $Rva -lt ($virtualAddress + $span)) {
            return [int]($rawAddress + ($Rva - $virtualAddress))
        }
    }
    throw ('RVA 0x{0:X} is not contained in a PE section' -f $Rva)
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

$addressId = 44204
if ($addressId -ge $offsetCount) { throw "Address Library does not contain ID $addressId" }
$functionRva = [BitConverter]::ToUInt32($libraryBytes, 96 + ($addressId * 4))
if ($functionRva -ne 0x8009C0) {
    throw ('Address Library ID 44204 resolved to unexpected RVA 0x{0:X}' -f $functionRva)
}

$callsiteRva = [uint32]($functionRva + 0x3AA)
$exeBytes = [IO.File]::ReadAllBytes($SkyrimExe)
$callsiteOffset = Convert-RvaToFileOffset $exeBytes $callsiteRva
$prefix = [byte[]](0x48, 0x8D, 0x55, 0xC0, 0x48, 0x8B, 0xCE)
$suffix = [byte[]](0x90, 0x48, 0x8D, 0x4D, 0xC0)
for ($index = 0; $index -lt $prefix.Length; ++$index) {
    if ($exeBytes[$callsiteOffset - $prefix.Length + $index] -ne $prefix[$index]) {
        throw "Projectile hit callsite prefix does not match"
    }
}
if ($exeBytes[$callsiteOffset] -ne 0xE8) { throw "Projectile hit callsite is not a direct call" }
for ($index = 0; $index -lt $suffix.Length; ++$index) {
    if ($exeBytes[$callsiteOffset + 5 + $index] -ne $suffix[$index]) {
        throw "Projectile hit callsite suffix does not match"
    }
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
    addressLibraryId = $addressId
    functionRva = ('0x{0:X}' -f $functionRva)
    callsiteRva = ('0x{0:X}' -f $callsiteRva)
    signature = '48 8D 55 C0 48 8B CE E8 ?? ?? ?? ?? 90 48 8D 4D C0'
    verified = $true
} | ConvertTo-Json -Depth 4
