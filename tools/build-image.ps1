<#
.SYNOPSIS
Builds an authored Xbox 360 image from real generated inputs in a fixed external workspace.
.DESCRIPTION
Sources and generated inputs are mounted read-only. Scratch, object caches and packaged
outputs use separate explicit mounts; no checkout copy or repository-local imports are made.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GeneratedCoreRoot,
    [Parameter(Mandatory = $true)][string]$ContentRoot,
    [string]$CatalogRoot = '',
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [string]$NativeObjectCacheRoot = '',
    [string]$NativeScratchRoot = $env:HELENGINE_X360_NATIVE_SCRATCH_ROOT,
    [string]$BeforeNativeValidationScript = $env:HELENGINE_X360_BEFORE_NATIVE_VALIDATION_SCRIPT,
    [switch]$ValidateScratchOnly,
    [string]$Config = $env:HELENGINE_X360_NATIVE_CONFIG,
    [string[]]$Define = @('HEL_DEMO_MODE=0')
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Config)) { $Config = 'tools/config.local.json' }
$env:PYTHONDONTWRITEBYTECODE = '1'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'build-paths.ps1')
$generated = Resolve-X360HostPath ((Resolve-Path -LiteralPath $GeneratedCoreRoot).Path) $repo
$content = Resolve-X360HostPath ((Resolve-Path -LiteralPath $ContentRoot).Path) $repo
$output = Assert-X360ExternalOutput ([IO.Path]::GetFullPath($OutputRoot)) $repo
$sessionRoot = if ([string]::IsNullOrWhiteSpace($NativeScratchRoot)) { Join-Path $output 'native' } else { [IO.Path]::GetFullPath($NativeScratchRoot) }
$sessionRoot = Assert-X360ExternalOutput $sessionRoot $repo
$nativeCache = if ([string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) { Join-Path $sessionRoot 'cache' } else { [IO.Path]::GetFullPath($NativeObjectCacheRoot) }
$nativeCache = Assert-X360ExternalOutput $nativeCache $repo
foreach ($inputRoot in @($generated, $content, $CatalogRoot) | Where-Object { $_ }) {
    $inputFull = (Resolve-X360HostPath $inputRoot $repo).TrimEnd('\', '/')
    foreach ($writeRoot in @($sessionRoot, $output, $nativeCache)) {
        if ($writeRoot.Equals($inputFull, [StringComparison]::OrdinalIgnoreCase) -or
            $writeRoot.StartsWith($inputFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            $inputFull.StartsWith($writeRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Native output and input roots must not overlap.'
        }
    }
}
$ownershipPath = Join-Path $sessionRoot 'native-scratch-owner.json'
$existed = Test-Path -LiteralPath $sessionRoot
if ($existed -and -not (Test-Path -LiteralPath $ownershipPath -PathType Leaf)) { throw 'Existing fixed native scratch has no ownership receipt; it was preserved.' }
New-Item -ItemType Directory -Force -Path $sessionRoot, $output, $nativeCache | Out-Null
$scratchLock = $null
try {
    $scratchLock = [IO.File]::Open((Join-Path $sessionRoot 'native-scratch.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    if ($existed) {
        $owner = Get-Content -LiteralPath $ownershipPath -Raw | ConvertFrom-Json
        if ($owner.schema -ne 1 -or $owner.repositoryRoot -ne $repo -or $owner.outputRoot -ne $output) { throw 'Fixed native scratch belongs to another output; it was preserved.' }
    }
    [ordered]@{schema=1;repositoryRoot=$repo;outputRoot=$output;nativeScratchRoot=$sessionRoot;configuredBy='NativeScratchRoot / HELENGINE_X360_NATIVE_SCRATCH_ROOT'} | ConvertTo-Json | Set-Content -LiteralPath $ownershipPath
    if ($ValidateScratchOnly) {
        Write-Output "Verified fixed SDK scratch: $sessionRoot; output owner: $output; exclusive lock acquired."
        return
    }
    if (-not [string]::IsNullOrWhiteSpace($BeforeNativeValidationScript)) {
        $validationScript = (Resolve-Path -LiteralPath $BeforeNativeValidationScript).Path
        if ([IO.Path]::GetExtension($validationScript) -ne '.ps1') { throw 'Native input validation requires a local PowerShell script.' }
        & $validationScript -GeneratedCoreRoot $generated
    }
    $coreLibDir = Join-Path $sessionRoot 'core'
    & (Join-Path $PSScriptRoot 'build-core.ps1') -Config $Config -GeneratedRoot $generated -OutDir $coreLibDir -NativeObjectCacheRoot $nativeCache -Jobs 2
    if ($LASTEXITCODE -ne 0) { throw 'core compilation failed' }
    $catalogArguments = @{}
    if ($CatalogRoot) { $catalogArguments['CatalogRoot'] = (Resolve-Path -LiteralPath $CatalogRoot).Path }
    $imageDir = Join-Path $sessionRoot 'image-staging'
    & (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -OutDir $imageDir -CoreGenerated $generated -CoreLib (Join-Path $coreLibDir 'libhelcore.a') -ContentRoot $content -Define $Define -NativeObjectCacheRoot $nativeCache @catalogArguments
    if ($LASTEXITCODE -ne 0) { throw 'image link failed' }
    foreach ($name in @('helengine.xex', 'helengine.elf', 'hashes.txt')) {
        $built = Join-Path $imageDir $name
        if (-not (Test-Path -LiteralPath $built -PathType Leaf)) { throw "The link step produced no $built" }
        Copy-Item -LiteralPath $built -Destination (Join-Path $output $name) -Force
    }
    Write-Host "Image written to $output/helengine.xex"
} finally {
    $sessionBuildLog = Join-Path $sessionRoot 'image-staging/build.log'
    if (-not $ValidateScratchOnly -and (Test-Path -LiteralPath $sessionBuildLog -PathType Leaf)) {
        Copy-Item -LiteralPath $sessionBuildLog -Destination (Join-Path $output 'native-build.log') -Force
    }
    if ($scratchLock) { $scratchLock.Dispose() }
}
