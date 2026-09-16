<#
.SYNOPSIS
Compiles the transpiled engine core (build/generated-core) into a static
archive the guest image links against.

.DESCRIPTION
Compiles the generator's own unity translation unit, which includes every
generated source once. That is not an optimisation: the generated code
instantiates templates (ContentManager::RegisterProcessor<T>,
AssetContentProcessor_1<T> and friends) whose definitions live in other
generated sources, so per-file compilation links with undefined references.
The PS1 and Dreamcast ports solve this the same way, with a finalized
single translation unit.

Uses the same machine and ABI flags as the rest of the guest, with one
deliberate difference: the core is compiled WITHOUT -ffreestanding. The
generated runtime includes <functional> and <memory> unconditionally, and
libstdc++'s headers refuse to compile in freestanding mode, so the core is
compiled against the hosted headers while the image is still linked with
-nostdlib. The library symbols that reach the link are listed in
src/platform/xenia_guest/X360RuntimeProvider.hpp.

Generated code is not held to -Werror: it is machine output, and a warning
in it is a codegen question, not a guest bug. Compile errors are reported
with the first failing files so a broken capability is easy to find.

.EXAMPLE
./tools/build-core.ps1 -GeneratedRoot build/generated-core/generated
#>
[CmdletBinding()]
param(
    [string]$Config = "tools/config.local.json",
    [string]$GeneratedRoot = "build/generated-core/generated",
    [string]$OutDir = "build/core",
    [int]$Jobs = 0
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$generatedFull = Join-Path $repo $GeneratedRoot
if (-not (Test-Path $generatedFull)) {
    throw "No generated core at $generatedFull. Run tools/generate-core.ps1 first."
}
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$outRel = $OutDir.Replace("\", "/")
$genRel = $GeneratedRoot.Replace("\", "/")
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

$script = @"
set -uo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:`$PATH
OUT=/work/$outRel
GEN=/work/$genRel
# ar adds to an existing archive, so a stale one would keep objects that are
# no longer built.
rm -rf `$OUT/obj `$OUT/errors.txt `$OUT/failed.txt `$OUT/libhelcore.a
mkdir -p `$OUT/obj
FLAGS="-mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec -mhard-float -mstrict-align -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-pic -fno-common -O2 -std=c++20 -fno-exceptions -frtti -fno-threadsafe-statics -fno-unwind-tables -I`$GEN -I/work/src/platform/xenia_guest -w"
UNITY=`$GEN/generated_unity.cpp
if [ ! -f "`$UNITY" ]; then echo "no generated_unity.cpp in `$GEN"; exit 1; fi
echo "translation unit: `$UNITY (`$(grep -c '#include' "`$UNITY") generated sources)"
if ! powerpc-eabi-g++ `$FLAGS -c "`$UNITY" -o `$OUT/obj/helcore.o 2> `$OUT/errors.txt; then
  echo "`$UNITY" > `$OUT/failed.txt
  echo "failed: 1"
  exit 1
fi
touch `$OUT/failed.txt
echo "failed: 0"
powerpc-eabi-ar rcs `$OUT/libhelcore.a `$OUT/obj/*.o
powerpc-eabi-nm `$OUT/libhelcore.a > `$OUT/libhelcore.nm
echo "archive: `$(stat -c %s `$OUT/libhelcore.a) bytes"
"@
$scriptPath = Join-Path $out "build-core.sh"
[System.IO.File]::WriteAllText($scriptPath, $script.Replace("`r`n", "`n"))

$env:MSYS_NO_PATHCONV = "1"
& docker run --rm -v "${repo}:/work" $cfg.docker_image bash "/work/$outRel/build-core.sh"
$code = $LASTEXITCODE
$errorsPath = Join-Path $out "errors.txt"
$failedPath = Join-Path $out "failed.txt"
if ($code -ne 0) {
    if (Test-Path $failedPath) {
        $failed = @(Get-Content $failedPath)
        Write-Host "First failing translation units:"
        $failed | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
        Write-Host "Distinct error lines:"
        Get-Content $errorsPath |
            Select-String -Pattern "error:" |
            ForEach-Object { ($_ -replace '^.*error: ', '') } |
            Group-Object | Sort-Object Count -Descending |
            Select-Object -First 15 |
            ForEach-Object { Write-Host ("  {0,4}x {1}" -f $_.Count, $_.Name) }
    }
    throw "Core compilation failed; see $errorsPath"
}
Write-Host "Core archive: $out/libhelcore.a"
