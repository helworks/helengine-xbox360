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
    [string[]]$Define = @()
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$outRel = $OutDir.Replace("\", "/")

& (Join-Path $PSScriptRoot "build-shaders.ps1") -Config $Config | Out-Null
if ($LASTEXITCODE -ne 0) { throw "shader build failed" }

$cflags = "-mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec " +
          "-mhard-float -mstrict-align -ffreestanding -fno-builtin -nostdlib " +
          "-fno-stack-protector -fno-asynchronous-unwind-tables -fno-pic " +
          "-fno-common -std=c11 -O2 -Wall -Wextra -Werror -I/work/build/shaders"
foreach ($d in $Define) { $cflags += " -D$d" }

$sources = @(
    "src/platform/xenia_guest/start.S",
    "src/platform/xenia_guest/imports.S",
    "src/platform/xenia_guest/platform.c",
    "src/platform/xenia_guest/runtime.c",
    "src/platform/xenia_guest/video.c",
    "src/platform/xenia_guest/shader.c",
    "src/platform/xenia_guest/mesh.c",
    "src/engine/frame.c",
    "src/engine/math.c",
    "src/demo/triangle.c",
    "src/demo/cube.c",
    "src/demo/cube_demo.c",
    "src/demo/main.c"
)
$compileLines = @()
$objects = @()
foreach ($s in $sources) {
    $obj = "`$OUT/" + ($s -replace "[/\\]", "_") + ".o"
    $objects += $obj
    $compileLines += "powerpc-eabi-gcc $cflags -c /work/$s -o $obj"
}
$script = @"
set -euo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:`$PATH
OUT=/work/$outRel
powerpc-eabi-gcc --version | head -1 > `$OUT/toolchain.txt
powerpc-eabi-ld --version | head -1 >> `$OUT/toolchain.txt
$($compileLines -join "`n")
powerpc-eabi-gcc $cflags -nostartfiles -T /work/src/platform/xenia_guest/link.ld -Wl,-Map,`$OUT/helengine.map -o `$OUT/helengine.elf $($objects -join ' ') -lgcc
powerpc-eabi-objdump -d `$OUT/helengine.elf > `$OUT/helengine.dis
powerpc-eabi-nm -n `$OUT/helengine.elf > `$OUT/helengine.nm
powerpc-eabi-readelf -h -l -S `$OUT/helengine.elf > `$OUT/helengine.readelf.txt
"@
$scriptPath = Join-Path $out "build.sh"
[System.IO.File]::WriteAllText($scriptPath, $script.Replace("`r`n", "`n"))

$env:MSYS_NO_PATHCONV = "1"
& docker run --rm -v "${repo}:/work" $cfg.docker_image bash "/work/$outRel/build.sh"
if ($LASTEXITCODE -ne 0) { throw "docker build step failed with exit code $LASTEXITCODE" }

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
