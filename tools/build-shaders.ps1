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
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $out | Out-Null

# unittest reports on stderr; merge streams in cmd so PowerShell does not treat
# the report as an error record.
$testOutput = & cmd /c "`"$($cfg.python)`" `"$(Join-Path $repo 'tests/xenos_asm_test.py')`" 2>&1"
$testOutput | Select-Object -Last 2
if ($LASTEXITCODE -ne 0) { throw "xenos_asm tests failed" }

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
