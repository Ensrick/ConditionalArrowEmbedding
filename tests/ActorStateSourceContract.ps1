param([Parameter(Mandatory = $true)][string]$SourceRoot)
$ErrorActionPreference = 'Stop'
function Read-Code([string]$Name) {
    $code = Get-Content -LiteralPath (Join-Path $SourceRoot $Name) -Raw
    $code = [regex]::Replace($code, '/\*[\s\S]*?\*/', '')
    return [regex]::Replace($code, '//[^\r\n]*', '')
}
$hook = Read-Code 'Hooks.cpp'
$accessor = Read-Code 'ActorStateAccess.h'
if ($hook -notmatch '\.reanimated\s*=\s*IsRuntimeReanimated\s*\(\s*a_target\s*\)') {
    throw 'Eligibility must use the native-layout-tested actor-state accessor'
}
if ($hook -match 'a_target\s*(?:\.|->)\s*IsReanimated\s*\(') {
    throw 'Direct inherited Actor::IsReanimated reads the wrong multi-runtime layout'
}
if ($accessor -notmatch 'return\s+a_target\.AsActorState\(\)->IsReanimated\(\)\s*;') {
    throw 'The actor-state helper must use CommonLib runtime-aware base-class access'
}
'Actor-state production wiring contract passed'
