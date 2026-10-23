<#
.SYNOPSIS
Assembles shaders/*.xasm into build/shaders/<name>.{bin,h,lst} with
tools/xenos_asm.py and runs the assembler's encoding tests first.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$OutDir = "build/shaders"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $PSScriptRoot 'build-paths.ps1')
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Resolve-X360HostPath $OutDir $repo
New-Item -ItemType Directory -Force $out | Out-Null

# unittest reports on stderr even on success; keep it out of PS5 error records.
. (Join-Path $PSScriptRoot 'native-process.ps1')
$testResult = Invoke-X360NativeTool -FilePath $cfg.python -Arguments @((Join-Path $repo 'tests/xenos_asm_test.py')) -WorkingDirectory $repo
$testResult.Output.TrimEnd([char[]]"`r`n") -split "\r?\n" | Select-Object -Last 2
if ($testResult.ExitCode -ne 0) { throw "xenos_asm tests failed with exit code $($testResult.ExitCode)" }

foreach ($src in Get-ChildItem (Join-Path $repo "shaders") -Filter *.xasm) {
    $name = ($src.BaseName -replace "\.", "_")
    & $cfg.python (Join-Path $repo "tools/xenos_asm.py") $src.FullName `
        --out (Join-Path $out "$name.bin") --header (Join-Path $out "$name.h") `
        --symbol "hel_shader_$name" --listing (Join-Path $out "$name.lst")
    if ($LASTEXITCODE -ne 0) { throw "assembling $($src.Name) failed" }
}
Get-FileHash (Join-Path $out "*.bin") -Algorithm SHA256 |
    ForEach-Object { "{0}  {1}" -f $_.Hash, (Split-Path $_.Path -Leaf) } |
    Set-Content (Join-Path $out "hashes.txt")
Get-Content (Join-Path $out "hashes.txt")
