<#
.SYNOPSIS
Verifies build/probe/entry.elf and entry.xex against the pinned loader rules.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$OutDir = "build/probe"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
& $cfg.python (Join-Path $repo "tools/xexcheck.py") `
    --elf (Join-Path $out "entry.elf") `
    --xex (Join-Path $out "entry.xex") `
    --nm (Join-Path $out "entry.nm") `
    --imports (Join-Path $repo "probes/entry/imports.inc") `
    --require _start --require hel_probe_main --require hel_probe_result `
    --require hel_syscall_marker
if ($LASTEXITCODE -ne 0) { throw "probe checks failed" }
