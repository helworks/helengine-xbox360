<#
.SYNOPSIS
Compiles and runs the native-format and fenced-retirement host tests with the pinned devkitPPC image.
.DESCRIPTION
Reuses one visible workspace output directory. The real uploader is linked unchanged with physical-allocation stubs;
no guest image or emulator is started. The native layout helper and uploader are also compiled for PowerPC.
#>
[CmdletBinding()]
param(
    [string]$OutputRoot,
    [string]$Config = 'tools/config.local.json'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Split-Path $repoRoot -Parent) 'builds/helengine-x360/texture-formats/native-artifacts'
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$settings = Get-Content -LiteralPath (Join-Path $repoRoot $Config) -Raw | ConvertFrom-Json
$image = $settings.docker_image
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$mounts = @('run','--rm','--mount',"type=bind,source=$repoRoot,target=/source,readonly",
    '--mount',"type=bind,source=$OutputRoot,target=/out",'--env','TMPDIR=/out',$image)
$receipts = @()
foreach ($test in @('native_texture_format_tests','texture_retirement_tests')) {
    $sources = @("/source/tests/$test.c",'/source/src/platform/xenia_guest/texture.c',
        '/source/src/platform/xenia_guest/x360_native_texture.c')
    if ($test -eq 'texture_retirement_tests') { $sources += '/source/src/platform/xenia_guest/mesh.c' }
    & rtk proxy docker @mounts gcc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' @sources -o "/out/$test" *> "$OutputRoot/$test-compile.log"
    $compileExit = $LASTEXITCODE
    if ($compileExit -ne 0) { Get-Content "$OutputRoot/$test-compile.log" -Tail 25; throw "$test compile exit $compileExit" }
    & rtk proxy docker @mounts "/out/$test" *> "$OutputRoot/$test-run.log"
    $runExit = $LASTEXITCODE
    $receipts += @{test=$test;compile_exit=$compileExit;run_exit=$runExit}
    if ($runExit -ne 0) { Get-Content "$OutputRoot/$test-run.log" -Tail 25; throw "$test run exit $runExit" }
    Get-Content "$OutputRoot/$test-run.log" -Tail 3
}
foreach ($source in @('x360_native_texture','texture')) {
    & rtk proxy docker @mounts /opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc -std=c11 -mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec -mhard-float -mstrict-align -ffreestanding -fno-builtin -O2 -Wall -Wextra -Werror -c "/source/src/platform/xenia_guest/$source.c" -o "/out/$source-ppc.o" *> "$OutputRoot/$source-ppc-compile.log"
    $compileExit = $LASTEXITCODE
    $receipts += @{translation_unit="$source.c";powerpc_compile_exit=$compileExit}
    if ($compileExit -ne 0) { Get-Content "$OutputRoot/$source-ppc-compile.log" -Tail 25; throw "$source PowerPC compile exit $compileExit" }
}
@{image=$image;checks=$receipts} | ConvertTo-Json -Depth 5 | Set-Content "$OutputRoot/native-texture-receipt.json"
Write-Output "Native texture host tests and PowerPC translation units PASS: $OutputRoot"
