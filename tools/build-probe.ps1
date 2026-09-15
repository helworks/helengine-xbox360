<#
.SYNOPSIS
Builds probes/entry into build/probe/entry.elf and build/probe/entry.xex.

.DESCRIPTION
Compiles with the devkitPPC GNU toolchain (GCC 15.2.0, Binutils 2.45.1) inside
the pinned devkitpro/devkitppc Docker image, then wraps the ELF into an
unencrypted, uncompressed XEX2 with tools/xexpack.py. Every step checks its
exit code. The exact compiler flags are recorded here on purpose.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$OutDir = "build/probe"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$outRel = $OutDir.Replace("\", "/")

# Target flags: generic 32-bit big-endian PowerPC ISA (no Gekko/Broadway
# paired-single instructions), SVR4 calling convention, no small-data
# addressing through r2/r13 (Xenia's kernel owns r13), hard float, no runtime.
$cflags = "-mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec " +
          "-mhard-float -mstrict-align -ffreestanding -fno-builtin -nostdlib " +
          "-fno-stack-protector -fno-asynchronous-unwind-tables -fno-pic " +
          "-fno-common -std=c11 -O2 -Wall -Wextra -Werror"
$script = @"
set -euo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:`$PATH
cd /work/probes/entry
OUT=/work/$outRel
powerpc-eabi-gcc --version | head -1 > `$OUT/toolchain.txt
powerpc-eabi-ld --version | head -1 >> `$OUT/toolchain.txt
powerpc-eabi-gcc $cflags -c probe.c -o `$OUT/probe.o
powerpc-eabi-gcc $cflags -c start.S -o `$OUT/start.o
powerpc-eabi-gcc $cflags -c imports.S -o `$OUT/imports.o
powerpc-eabi-gcc $cflags -nostartfiles -T link.ld -Wl,-Map,`$OUT/entry.map -o `$OUT/entry.elf `$OUT/start.o `$OUT/probe.o `$OUT/imports.o -lgcc
powerpc-eabi-objdump -d `$OUT/entry.elf > `$OUT/entry.dis
powerpc-eabi-nm -n `$OUT/entry.elf > `$OUT/entry.nm
powerpc-eabi-readelf -h -l -S `$OUT/entry.elf > `$OUT/entry.readelf.txt
"@
$scriptPath = Join-Path $out "build.sh"
[System.IO.File]::WriteAllText($scriptPath, $script.Replace("`r`n", "`n"))

$env:MSYS_NO_PATHCONV = "1"
& docker run --rm -v "${repo}:/work" $cfg.docker_image bash "/work/$outRel/build.sh"
if ($LASTEXITCODE -ne 0) { throw "docker build step failed with exit code $LASTEXITCODE" }

& $cfg.python (Join-Path $repo "tools/xexpack.py") `
    --elf (Join-Path $out "entry.elf") `
    --nm (Join-Path $out "entry.nm") `
    --imports (Join-Path $repo "probes/entry/imports.inc") `
    --out (Join-Path $out "entry.xex") `
    --report (Join-Path $out "entry.xex.json")
if ($LASTEXITCODE -ne 0) { throw "xexpack failed with exit code $LASTEXITCODE" }

Get-FileHash (Join-Path $out "entry.elf"), (Join-Path $out "entry.xex") -Algorithm SHA256 |
    ForEach-Object { "{0}  {1}" -f $_.Hash, (Split-Path $_.Path -Leaf) } |
    Set-Content (Join-Path $out "hashes.txt")
Get-Content (Join-Path $out "toolchain.txt")
Get-Content (Join-Path $out "hashes.txt")
Write-Host "Built $out/entry.elf and $out/entry.xex"
