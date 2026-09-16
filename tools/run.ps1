<#
.SYNOPSIS
Runs build/xenia/helengine.xex in the pinned Xenia build and evaluates the
engine's log markers.

.DESCRIPTION
Pass criteria (all from the emulator log, never from the window alone):
"HEL video: first present complete" appears, at least one "HEL frame: frames"
report appears, and no "HEL frame: error" / "HEL demo: ... failed" line is
present. With -ExpectFailure the criteria invert: an init failure diagnostic
must appear and no presentation may happen.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$Target = "build/xenia/helengine.xex",
    [int]$TimeoutSeconds = 30,
    [string]$RunName = "",
    [string[]]$ExtraArgs = @(),
    [switch]$ExpectFailure,
    [int]$CaptureCount = 0,
    [int]$CaptureAfterMs = 8000,
    [int]$CaptureIntervalMs = 700
)
$ErrorActionPreference = "Stop"
$logDir = & (Join-Path $PSScriptRoot "xenia-launch.ps1") -Config $Config -Target $Target `
    -TimeoutSeconds $TimeoutSeconds -RunName $RunName -ExtraArgs $ExtraArgs `
    -CaptureCount $CaptureCount -CaptureAfterMs $CaptureAfterMs -CaptureIntervalMs $CaptureIntervalMs
$logDir = "$logDir".Trim()
if ($CaptureCount -gt 0 -and (Test-Path (Join-Path $logDir "captures.json"))) {
    Write-Host "Captures (client size, average RGB of the central region):"
    $captureList = Get-Content (Join-Path $logDir "captures.json") -Raw | ConvertFrom-Json
    foreach ($c in $captureList) {
        if ($c.file) { Write-Host ("  {0} {1} {2}x{3} rgb=({4})" -f $c.index, $c.time, $c.width, $c.height, ($c.avg_rgb -join ",")) }
        else { Write-Host ("  {0} {1} no window" -f $c.index, $c.time) }
    }
}
$log = Get-Content (Join-Path $logDir "xenia.log")
$hel = $log | Select-String -SimpleMatch "(DbgPrint) HEL "
$firstPresent = $hel | Select-String -SimpleMatch "first present complete"
$frames = $hel | Select-String -SimpleMatch "HEL frame: frames="
$errors = $hel | Select-String -Pattern "HEL frame: error|failed|fence timeout|rejected configuration|validator accepted"
$gpuErrors = $log | Select-String -Pattern "^!>" | Where-Object { $_ -notmatch "File not found: .*xam\.xex" }

Write-Host "Log: $(Join-Path $logDir 'xenia.log')"
Write-Host "HEL lines: $($hel.Count)"
$hel | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
if ($hel.Count -gt 40) { Write-Host "  ..."; $hel | Select-Object -Last 6 | ForEach-Object { Write-Host "  $_" } }
Write-Host "Emulator error lines:"; $gpuErrors | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }

$summary = [ordered]@{
    run = (Split-Path $logDir -Leaf); target = $Target
    first_present = ($firstPresent.Count -gt 0)
    frame_reports = @($frames | ForEach-Object { $_.Line })
    hel_errors = @($errors | ForEach-Object { $_.Line })
    emulator_errors = @($gpuErrors | ForEach-Object { $_.Line })
}
$summary | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $logDir "summary.json")

if ($ExpectFailure) {
    if ($firstPresent.Count -eq 0 -and $errors.Count -gt 0) { Write-Host "RESULT: failure path diagnosed, no presentation"; exit 0 }
    Write-Host "RESULT: expected a diagnosed failure"; exit 1
}
if ($firstPresent.Count -gt 0 -and $frames.Count -gt 0 -and $errors.Count -eq 0) {
    Write-Host "RESULT: presenting frames"; exit 0
}
Write-Host "RESULT: criteria not met"; exit 1
