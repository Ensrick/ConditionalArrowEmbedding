#Requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$audit = Join-Path $repo 'tools/verify-runtime-hook.ps1'
$source = [IO.File]::ReadAllText((Join-Path $repo 'src/Hooks.cpp'))
$scratch = Join-Path $repo 'build/release/audit-negative-control'
[IO.Directory]::CreateDirectory($scratch) | Out-Null
$cases = @(
    @{Name='missing-submission'; Old='QueueSubmissionCallOffset = 0xA7'; New='QueueSubmissionCallOffset = 0xA6'},
    @{Name='missing-visual-consumer'; Old='g_deferredImpacts.Consume('; New='g_deferredImpacts.Other('},
    @{Name='missing-lifecycle-guard'; Old='HasUnsupportedDeferredLifecycle(a_projectile)'; New='false'},
    @{Name='missing-handled-barrier'; Old='impact && impact->unk48 == 1'; New='impact && impact->unk48 == 0'}
)
foreach ($case in $cases) {
    if (-not $source.Contains($case.Old)) { throw "Negative control cannot find source token: $($case.Name)" }
    $path = Join-Path $scratch ($case.Name + '.cpp')
    try {
        [IO.File]::WriteAllText($path, $source.Replace($case.Old, $case.New))
        try {
            $ErrorActionPreference = 'Continue' # Expected native stderr is captured, not a test abort.
            $output = & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $audit -HookSource $path 2>&1
        } finally { $ErrorActionPreference = 'Stop' }
        if ($LASTEXITCODE -eq 0) { throw "Audit incorrectly accepted negative control: $($case.Name)" }
        if (($output | Out-String) -notmatch 'Missing queued-damage production contract') {
            throw "Negative control failed for an unexpected reason: $($case.Name): $output"
        }
        "PASS rejected $($case.Name)"
    } finally {
        if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) }
    }
}
