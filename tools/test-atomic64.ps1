param([string]$OutDir = 'C:\temp\helengine\x360')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out = [IO.Path]::GetFullPath($OutDir)
if (-not $out.StartsWith('C:\temp\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Host-test outputs must be below C:\temp.'
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:TEMP = $out
$env:TMP = $out
$object = Join-Path $out 'atomic64-host.o'
$exe = Join-Path $out 'atomic64-tests.exe'
& gcc -std=c11 -O2 -Wall -Wextra -Werror -c (Join-Path $repo 'src/platform/xenia_guest/atomic64.c') -o $object
if ($LASTEXITCODE -ne 0) { throw 'Compiling atomic64 runtime failed.' }
& g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread (Join-Path $repo 'tests/atomic64_tests.cpp') $object -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Linking atomic64 host tests failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Atomic64 host tests failed.' }
