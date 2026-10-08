<#
.SYNOPSIS
Transpiles the helengine C# core to C++ for the Xbox 360 guest.

.DESCRIPTION
Runs csharpcodegen's CLI with the capability profile this target can support:
32-bit big-endian PowerPC, no C++ standard library, no exceptions and no
hosted file system, with storage and services supplied by
src/platform/xenia_guest/X360RuntimeProvider.hpp and the math functions by
X360Math.hpp. The shape of the invocation follows the PS1 port's
scripts/generate-core-boot.ps1; the differences are the endianness, the
platform id and the two provider headers. RTTI stays on because the
generated generic dispatch requires it (codegen reports CPP1001 without it),
which is also what the PS1 port does; it costs the type-info objects in
libsupc++ at link time.

The output directory must not exist: a stale tree would silently mix
generated files from two runs. Nothing outside this repository is written;
the generator assembly is used where it was already built.

.EXAMPLE
./tools/generate-core.ps1 -OutputRoot build/generated-core
#>
[CmdletBinding()]
param(
    [string]$Config = "tools/config.local.json",
    [string]$OutputRoot = "build/generated-core",
    # Worktrees of the shared repositories, pinned to the branches this target
    # needs (docs/core-integration.md records which). They are used instead of
    # the shared checkouts so a regeneration here cannot be changed by, or
    # disturb, another session working in those repositories.
    [string]$EngineSourceRoot = "C:/dev/helworks/helengine-camera-fov",
    [string]$CodegenRoot = "C:/dev/helworks/csharpcodegen-x360",
    [switch]$Force
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$engineSource = (Resolve-Path -LiteralPath $EngineSourceRoot).Path
$codegen = (Resolve-Path -LiteralPath $CodegenRoot).Path
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputRoot))
if (Test-Path -LiteralPath $output) {
    if (-not $Force) { throw "Use a fresh output directory (or pass -Force): $output" }
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $output | Out-Null
$generated = Join-Path $output "generated"

$dotnet = "C:/Program Files/dotnet/dotnet.exe"
$generator = Join-Path $codegen "codegen/bin/Debug/net9.0/codegen.dll"
if (-not (Test-Path -LiteralPath $generator)) {
    throw "codegen.dll not found at $generator. Build it in the csharpcodegen repository first."
}

$arguments = @(
    $generator, "--cpp",
    "--project", (Join-Path $engineSource "engine/helengine.shader/helengine.shader.csproj"),
    "--output", $generated,
    "--runtime-root", (Join-Path $codegen "cs2.cpp"),
    "--compiler", "gcc", "--platform", "xbox360", "--endianness", "big",
    "--runtime", "custom-retro",
    "--feature-catalog",
    (Join-Path $engineSource "engine/helengine.editor/codegen/features/helengine-feature-catalog.json"),
    "--set", "generated-math-convention=engine-row-vector",
    "--set", "pointer-size-bytes=4",
    "--set", "include-project-defined-preprocessor-symbols=false",
    "--set", "additional-preprocessor-symbols=HELENGINE_CODEGEN_DISABLE_MENU_REFLECTION;HELENGINE_CODEGEN_DISABLE_RUNTIME_SCRIPT_REFLECTION;HELENGINE_CODEGEN_FATAL_ONLY;HELENGINE_RUNTIME_MATERIAL_RESOLUTION_COOKED_PLATFORM_OWNED",
    "--set", "codegen-enabled-features=shaders",
    "--set", "codegen-forced-disabled-features=debug_overlay",
    "--set", "load-native-runtime-metadata=false",
    "--set", "codegen-use-std-string=false",
    "--set", "codegen-use-std-vector=false",
    "--set", "codegen-use-std-unordered-map=false",
    "--set", "codegen-use-std-unordered-set=false",
    "--set", "codegen-use-std-shared-ptr=false",
    "--set", "codegen-use-std-function=false",
    "--set", "codegen-use-std-chrono=false",
    "--set", "codegen-use-std-math=false",
    "--set", "codegen-runtime-math-header=X360Math.hpp",
    "--set", "codegen-use-hosted-file-system=false",
    "--set", "codegen-use-rtti=true",
    "--set", "codegen-use-exceptions=false",
    "--set", "codegen-runtime-provider-header=X360RuntimeProvider.hpp",
    # Full exception text: a console image has no other way to say what the
    # managed runtime objected to, and the size cost is small next to the
    # value of a readable failure.
    "--set", "codegen-compact-native-exception-messages=false"
)
$log = Join-Path $output "generation.log"
& $dotnet @arguments *> $log
if ($LASTEXITCODE -ne 0) {
    Get-Content $log -Tail 20
    throw "Core conversion failed with exit code $LASTEXITCODE; see $log"
}

$headers = @(Get-ChildItem -Path $generated -Filter *.hpp -Recurse -File)
$sources = @(Get-ChildItem -Path $generated -Filter *.cpp -Recurse -File)
$bytes = ($headers + $sources | Measure-Object -Property Length -Sum).Sum
Write-Host ("Generated {0} headers and {1} sources ({2:N1} MiB) in {3}" -f `
    $headers.Count, $sources.Count, ($bytes / 1MB), $generated)
Write-Host "Log: $log"
