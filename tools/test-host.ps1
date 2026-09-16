<#
.SYNOPSIS
Builds and runs the host tests: engine math and the shader
assembler encoding tests. Uses the host C compiler on PATH (gcc) for the C
tests; the engine sources are compiled unchanged.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$OutDir = "build/host-tests"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$gcc = (Get-Command gcc -ErrorAction Stop).Source
$failed = 0

foreach ($t in @(
    @{ name = "math_test"; sources = @("tests/math_test.c", "src/engine/math.c") }
)) {
    $exe = Join-Path $out "$($t.name).exe"
    $srcs = $t.sources | ForEach-Object { Join-Path $repo $_ }
    & $gcc -std=c11 -O1 -Wall -Wextra -DHEL_HOST_TEST=1 -o $exe @srcs
    if ($LASTEXITCODE -ne 0) { throw "compiling $($t.name) failed" }
    & $exe
    if ($LASTEXITCODE -ne 0) { $failed++ }
}

$testOutput = & cmd /c "`"$($cfg.python)`" `"$(Join-Path $repo 'tests/xenos_asm_test.py')`" 2>&1"
$testOutput | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { $failed++ }

if ($failed) { Write-Host "HOST TESTS: $failed suite(s) failed"; exit 1 }
Write-Host "HOST TESTS: all passed"
