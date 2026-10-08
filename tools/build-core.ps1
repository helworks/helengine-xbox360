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
    [string]$NativeObjectCacheRoot = '',
    [int]$Jobs = 0
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$generatedFull = Join-Path $repo $GeneratedRoot
if (-not (Test-Path $generatedFull)) {
    throw "No generated core at $generatedFull. Run tools/generate-core.ps1 first."
}
$out = if ([string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) { Join-Path $repo $OutDir } else { Join-Path ([IO.Path]::GetFullPath($NativeObjectCacheRoot)) 'x360-core' }
New-Item -ItemType Directory -Force $out | Out-Null
$scriptOut = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $scriptOut | Out-Null
$outRel = $OutDir.Replace("\", "/")
$genRel = $GeneratedRoot.Replace("\", "/")
$cacheFingerprintPath = Join-Path $out 'build-fingerprint.txt'
$fingerprintInput = @(
    $cfg.docker_image
    (Get-FileHash -LiteralPath (Join-Path $repo 'tools/build-core.ps1') -Algorithm SHA256).Hash
    '-mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec -mhard-float -mstrict-align -fno-builtin -O2 -std=c++20 -fno-exceptions -frtti'
    Get-ChildItem -LiteralPath (Join-Path $repo 'src/platform/xenia_guest') -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($repo.Length).TrimStart([char[]]@('\', '/'))
        "$relative=$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)"
    }
    Get-ChildItem -LiteralPath $generatedFull -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($generatedFull.Length).TrimStart([char[]]@('\', '/'))
        "$relative=$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)"
    }
) -join "`n"
$fingerprintBytes = [Text.Encoding]::UTF8.GetBytes($fingerprintInput)
$fingerprintSha = [Security.Cryptography.SHA256]::Create()
$cacheFingerprint = ([BitConverter]::ToString($fingerprintSha.ComputeHash($fingerprintBytes))).Replace('-', '').ToLowerInvariant()
$oldFingerprint = if (Test-Path -LiteralPath $cacheFingerprintPath) { (Get-Content -LiteralPath $cacheFingerprintPath -Raw).Trim() } else { '' }
if ($oldFingerprint -ne $cacheFingerprint) {
    foreach ($cached in @('obj/helcore.o', 'libhelcore.a', 'libhelcore.nm', 'failed.txt', 'errors.txt')) {
        $path = Join-Path $out $cached
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
$containerOut = if ([string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) { "/work/$outRel" } else { '/nativecache/x360-core' }

$script = @"
set -euo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:`$PATH
OUT=$containerOut
GEN=/work/$genRel
if [ -d /generated ]; then GEN=/generated; fi
mkdir -p `$OUT/obj
FLAGS="-mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec -mhard-float -mstrict-align -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-pic -fno-common -O2 -std=c++20 -fno-exceptions -frtti -fno-threadsafe-statics -fno-unwind-tables -I`$GEN -I/work/src/platform/xenia_guest -w"
# An editor build emits two unity translation units: generated_unity.cpp holds
# the engine core alone and exists to prove it compiles, while
# helengine_core_unity.cpp is the finalized one that also carries the project's
# own scripts. Compiling the first would leave every authored component
# undefined at link time, so the finalized one wins wherever it exists. A core
# generated by tools/generate-core.ps1 has only the first.
UNITY=`$GEN/helengine_core_unity.cpp
if [ ! -f "`$UNITY" ]; then UNITY=`$GEN/generated_unity.cpp; fi
if [ ! -f "`$UNITY" ]; then echo "no unity translation unit in `$GEN"; exit 1; fi
echo "translation unit: `$UNITY (`$(grep -c '#include' "`$UNITY") generated sources)"
if [ ! -f `$OUT/obj/helcore.o ]; then
  if ! powerpc-eabi-g++ `$FLAGS -c "`$UNITY" -o `$OUT/obj/helcore.o 2> `$OUT/errors.txt; then
  echo "`$UNITY" > `$OUT/failed.txt
  echo "failed: 1"
  exit 1
  fi
else
  echo "reused cached helcore.o"
fi
touch `$OUT/failed.txt
echo "failed: 0"
rm -f `$OUT/libhelcore.a
powerpc-eabi-ar rcsD `$OUT/libhelcore.a `$OUT/obj/*.o
powerpc-eabi-nm `$OUT/libhelcore.a > `$OUT/libhelcore.nm
echo "archive: `$(stat -c %s `$OUT/libhelcore.a) bytes"
"@
$scriptPath = Join-Path $scriptOut "build-core.sh"
[System.IO.File]::WriteAllText($scriptPath, $script.Replace("`r`n", "`n"))

$env:MSYS_NO_PATHCONV = "1"
$dockerArgs = @('run', '--rm', '-v', "${repo}:/work")
if (-not [string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) { $dockerArgs += @('-v', "${NativeObjectCacheRoot}:/nativecache") }
$dockerArgs += @($cfg.docker_image, 'bash', "/work/$($OutDir.Replace('\', '/'))/build-core.sh")
& docker @dockerArgs
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
if (-not [string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) {
    Set-Content -LiteralPath $cacheFingerprintPath -Value $cacheFingerprint -Encoding ASCII
    $publishDir = Join-Path $repo $OutDir
    New-Item -ItemType Directory -Force $publishDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $out 'libhelcore.a') -Destination (Join-Path $publishDir 'libhelcore.a') -Force
}
