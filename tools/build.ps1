<#
.SYNOPSIS
Builds the helengine Xenia guest image: build/xenia/helengine.elf and
build/xenia/helengine.xex (plus map, disassembly, symbols, readelf, hashes).

.DESCRIPTION
Same toolchain, flags and packer as tools/build-probe.ps1 (recorded in
docs/xenia-compatibility.md). -Define adds preprocessor definitions such as
HEL_DEMO_INVALID_CONFIG=1. Every step checks its exit code.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$OutDir = "build/xenia",
    [string]$CoreGenerated = "build/generated-core/generated",
    [string]$CoreLib = "build/core/libhelcore.a",
    [string[]]$Define = @()
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$outRel = $OutDir.Replace("\", "/")
$coreGenRel = $CoreGenerated.Replace("\", "/")
$coreLibRel = $CoreLib.Replace("\", "/")
if (-not (Test-Path (Join-Path $repo $CoreLib))) {
    throw "No core archive at $CoreLib. Run tools/build-core.ps1 first."
}

& (Join-Path $PSScriptRoot "build-shaders.ps1") -Config $Config | Out-Null
if ($LASTEXITCODE -ne 0) { throw "shader build failed" }

# Shared machine/ABI flags. -msdata=none keeps r13 (the KPCR in Xenia)
# untouched; -mno-eabi drops the EABI helper calls no runtime provides here.
$common = "-mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec " +
          "-mhard-float -mstrict-align -ffreestanding -fno-builtin -nostdlib " +
          "-fno-stack-protector -fno-asynchronous-unwind-tables -fno-pic " +
          "-fno-common -O2 -Wall -Wextra -Werror -I/work/build/shaders"
foreach ($d in $Define) { $common += " -D$d" }
$cflags = "$common -std=c11"
# The transpiled engine core is C++: freestanding, no C++ library, no
# exceptions or RTTI, no thread-safe static guards (the guest is single
# threaded). src/platform/xenia_guest/cxx_runtime.cpp supplies what the
# compiler still emits calls to.
$cxxflags = "$common -std=c++17 -fno-exceptions -fno-rtti " +
            "-fno-threadsafe-statics -fno-unwind-tables"
# Sources that include the transpiled core or its runtime need the hosted
# libstdc++ headers (the generated runtime includes <functional> and <memory>
# unconditionally) and C++20, which is what the generator emits. They are
# still linked into the same freestanding image; see build-core.ps1 and
# src/platform/xenia_guest/X360RuntimeProvider.hpp.
$coreCxxflags = (($common -replace " -ffreestanding", "") -replace " -Werror", "") +
                " -std=c++20 -fno-exceptions -frtti -fno-threadsafe-statics " +
                "-fno-unwind-tables -I/work/$coreGenRel -I/work/src/platform/xenia_guest"

# Compiled against the generated core (hosted headers, C++20).
$coreSources = @(
    "src/platform/xenia_guest/X360RuntimeProvider.cpp",
    "src/platform/xenia_guest/X360Math.cpp",
    "src/platform/xenia_guest/X360LibcShim.cpp",
    "src/demo/core_boot.cpp"
)
$sources = @(
    "src/platform/xenia_guest/start.S",
    "src/platform/xenia_guest/imports.S",
    "src/platform/xenia_guest/platform.c",
    "src/platform/xenia_guest/runtime.c",
    "src/platform/xenia_guest/heap.c",
    "src/platform/xenia_guest/cxx_runtime.cpp",
    "src/platform/xenia_guest/video.c",
    "src/platform/xenia_guest/shader.c",
    "src/platform/xenia_guest/mesh.c",
    "src/engine/frame.c",
    "src/engine/math.c",
    "src/demo/triangle.c",
    "src/demo/cube.c",
    "src/demo/cube_demo.c",
    "src/demo/cxx_selftest.cpp",
    "src/demo/main.c"
)
$compileLines = @()
$objects = @()
foreach ($s in ($sources + $coreSources)) {
    $obj = "`$OUT/" + ($s -replace "[/\\]", "_") + ".o"
    $objects += $obj
    if ($coreSources -contains $s) {
        $compileLines += "powerpc-eabi-g++ $coreCxxflags -c /work/$s -o $obj"
    } elseif ($s.EndsWith(".cpp")) {
        $compileLines += "powerpc-eabi-g++ $cxxflags -c /work/$s -o $obj"
    } else {
        $compileLines += "powerpc-eabi-gcc $cflags -c /work/$s -o $obj"
    }
}
$script = @"
set -euo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:`$PATH
OUT=/work/$outRel
powerpc-eabi-gcc --version | head -1 > `$OUT/toolchain.txt
powerpc-eabi-g++ --version | head -1 >> `$OUT/toolchain.txt
powerpc-eabi-ld --version | head -1 >> `$OUT/toolchain.txt
$($compileLines -join "`n")
powerpc-eabi-g++ $cxxflags -nostartfiles -T /work/src/platform/xenia_guest/link.ld -Wl,-Map,`$OUT/helengine.map -o `$OUT/helengine.elf $($objects -join ' ') /work/$coreLibRel -lstdc++ -lsupc++ -lm -lgcc
powerpc-eabi-objdump -d `$OUT/helengine.elf > `$OUT/helengine.dis
powerpc-eabi-nm -n `$OUT/helengine.elf > `$OUT/helengine.nm
powerpc-eabi-readelf -h -l -S `$OUT/helengine.elf > `$OUT/helengine.readelf.txt
"@
$scriptPath = Join-Path $out "build.sh"
[System.IO.File]::WriteAllText($scriptPath, $script.Replace("`r`n", "`n"))

$env:MSYS_NO_PATHCONV = "1"
# Run through cmd with the output redirected: Windows PowerShell turns any
# native stderr line into a terminating error under ErrorActionPreference
# Stop, and the compiler writes warnings there even on a successful build.
$buildLog = Join-Path $out "build.log"
& cmd /c "docker run --rm -v ""${repo}:/work"" $($cfg.docker_image) bash ""/work/$outRel/build.sh"" > ""$buildLog"" 2>&1"
if ($LASTEXITCODE -ne 0) {
    Get-Content $buildLog | Select-Object -Last 25
    throw "docker build step failed with exit code $LASTEXITCODE (see $buildLog)"
}
$warnings = @(Get-Content $buildLog | Select-String -Pattern "warning:")
if ($warnings.Count -gt 0) { Write-Host "$($warnings.Count) compiler warning(s); see $buildLog" }

$imports = Join-Path $repo "src/platform/xenia_guest/imports.inc"
& $cfg.python (Join-Path $repo "tools/xexpack.py") `
    --elf (Join-Path $out "helengine.elf") --nm (Join-Path $out "helengine.nm") `
    --imports $imports --out (Join-Path $out "helengine.xex") `
    --report (Join-Path $out "helengine.xex.json") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "xexpack failed with exit code $LASTEXITCODE" }

& $cfg.python (Join-Path $repo "tools/xexcheck.py") `
    --elf (Join-Path $out "helengine.elf") --xex (Join-Path $out "helengine.xex") `
    --nm (Join-Path $out "helengine.nm") --imports $imports `
    --require _start --require hel_demo_main --require hel_video_init `
    --require hel_frame_present --require hel_vdswap | Select-String -Pattern "FAIL|failure"
if ($LASTEXITCODE -ne 0) { throw "xexcheck failed" }

Get-FileHash (Join-Path $out "helengine.elf"), (Join-Path $out "helengine.xex") -Algorithm SHA256 |
    ForEach-Object { "{0}  {1}" -f $_.Hash, (Split-Path $_.Path -Leaf) } |
    Set-Content (Join-Path $out "hashes.txt")
Get-Content (Join-Path $out "hashes.txt")
Write-Host "Built $out/helengine.xex"
