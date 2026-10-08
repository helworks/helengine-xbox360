<#
.SYNOPSIS
Single entry point the platform builder calls to produce an Xbox 360 image.

.DESCRIPTION
The editor's build graph regenerates the core and cooks content; this takes
those outputs and turns them into a linked image. It compiles the generated
core, packs the staged content into the archive that travels inside the
image, and links the guest against both.

It exists so the C# builder has one thing to invoke, the way the PS1 port's
scripts/build.ps1 does, rather than knowing about the two steps underneath.
Running it by hand is also the way to reproduce an editor build outside the
editor.

.EXAMPLE
./tools/build-image.ps1 -GeneratedCoreRoot build/generated-core/generated `
    -ContentRoot build/content-staging -CatalogRoot build/catalog -OutputRoot build/image
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GeneratedCoreRoot,
    [Parameter(Mandatory = $true)][string]$ContentRoot,
    [string]$CatalogRoot = "",
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [string]$NativeObjectCacheRoot = '',
    [string]$NativeScratchRoot = $env:HELENGINE_X360_NATIVE_SCRATCH_ROOT,
    [string]$BeforeNativeValidationScript = $env:HELENGINE_X360_BEFORE_NATIVE_VALIDATION_SCRIPT,
    [switch]$ValidateScratchOnly,
    [string]$Config = "tools/config.local.json",
    [string[]]$Define = @("HEL_DEMO_MODE=0")
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$generated = (Resolve-Path -LiteralPath $GeneratedCoreRoot).Path
$output = [IO.Path]::GetFullPath($OutputRoot)
$nativeCache = if ([string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) { Join-Path $output 'native-cache' } else { [IO.Path]::GetFullPath($NativeObjectCacheRoot) }
New-Item -ItemType Directory -Force $output | Out-Null
$fixedScratch = -not [string]::IsNullOrWhiteSpace($NativeScratchRoot)
if ($ValidateScratchOnly -and -not $fixedScratch) { throw 'Scratch-only validation requires an explicit fixed NativeScratchRoot.' }
$scratchLock = $null
if ($fixedScratch) {
    # The SDK container mounts this repository only. Fixed scratch must stay in its
    # build tree, with an ownership receipt binding reuse to the caller output.
    $sessionRoot = [IO.Path]::GetFullPath($NativeScratchRoot).TrimEnd('\', '/')
    $buildPrefix = [IO.Path]::GetFullPath((Join-Path $repo 'build')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $sessionRoot.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixed native scratch must be below the Xbox 360 repository build root.' }
    $ancestor = $sessionRoot
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Fixed native scratch cannot cross a filesystem link.' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    $existed = Test-Path -LiteralPath $sessionRoot
    $ownershipPath = Join-Path $sessionRoot 'native-scratch-owner.json'
    if ($existed -and -not (Test-Path -LiteralPath $ownershipPath -PathType Leaf)) { throw 'Existing fixed native scratch has no ownership receipt; it was preserved.' }
    New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null
    try {
        $scratchLock = [IO.File]::Open((Join-Path $sessionRoot 'native-scratch.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        if ($existed) {
            $owner = Get-Content -LiteralPath $ownershipPath -Raw | ConvertFrom-Json
            if ($owner.schema -ne 1 -or $owner.repositoryRoot -ne $repo -or $owner.outputRoot -ne $output) { throw 'Fixed native scratch belongs to another output; it was preserved.' }
        }
        [ordered]@{schema=1;repositoryRoot=$repo;outputRoot=$output;nativeScratchRoot=$sessionRoot;configuredBy='NativeScratchRoot / HELENGINE_X360_NATIVE_SCRATCH_ROOT';updatedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $ownershipPath
    } catch {
        if ($scratchLock) { $scratchLock.Dispose() }
        throw
    }
    $sessionRelativeRoot = $sessionRoot.Substring($repo.Length).TrimStart('\', '/').Replace('\', '/')
} else {
    $sessionName = 'editor-' + [Guid]::NewGuid().ToString('N')
    $sessionRoot = Join-Path $repo (Join-Path 'build' $sessionName)
    $sessionRelativeRoot = "build/$sessionName"
    New-Item -ItemType Directory -Path $sessionRoot | Out-Null
}
try {
if ($ValidateScratchOnly) {
    Write-Output "Verified fixed SDK scratch: $sessionRoot; output owner: $output; exclusive lock acquired."
    return
}
if (-not [string]::IsNullOrWhiteSpace($BeforeNativeValidationScript)) {
    $validationScript = (Resolve-Path -LiteralPath $BeforeNativeValidationScript).Path
    if (-not (Test-Path -LiteralPath $validationScript -PathType Leaf) -or [IO.Path]::GetExtension($validationScript) -ne '.ps1') { throw 'Native input validation requires a local PowerShell script.' }
    & $validationScript -GeneratedCoreRoot $generated
}

# The container mounts this repository and nothing else, so anything the compiler
# has to read must be inside it. An editor build's generated core and staged
# content live in the editor's own build workspace, which is not, so they are
# copied in. Inputs that are already inside are used where they are, which is
# what a hand-run build does.
$importRoot = Join-Path $sessionRoot 'from-editor'
function ConvertTo-RepoRelative([string]$path, [string]$importName) {
    $full = [IO.Path]::GetFullPath($path)
    $repoPrefix = [IO.Path]::GetFullPath($repo).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($full.Equals([IO.Path]::GetFullPath($repo), [StringComparison]::OrdinalIgnoreCase)) {
        return ''
    }
    if ($full.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($repo.Length).TrimStart('\', '/').Replace('\', '/')
    }
    if (-not (Test-Path -LiteralPath $full)) {
        throw "build-image.ps1 was given a path that does not exist: $path"
    }
    $destination = Join-Path $importRoot $importName
    $resolvedImportRoot = [IO.Path]::GetFullPath($importRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolvedDestination = [IO.Path]::GetFullPath($destination)
    if (-not $resolvedDestination.StartsWith($resolvedImportRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Imported build input would escape its isolated staging root: $resolvedDestination"
    }
    if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
    New-Item -ItemType Directory -Force $destination | Out-Null
    & robocopy $full $destination /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copying '$path' into the repository failed with robocopy exit code $LASTEXITCODE" }
    Write-Host "Imported $importName from outside the repository"
    return "$sessionRelativeRoot/from-editor/$importName"
}

$generatedRelative = ConvertTo-RepoRelative $generated "generated-core"
$contentRelative = ConvertTo-RepoRelative $ContentRoot "content"
$coreLibDir = "$sessionRelativeRoot/core"

Write-Host "Compiling the generated core from $generatedRelative"
& (Join-Path $PSScriptRoot "build-core.ps1") -Config $Config -GeneratedRoot $generatedRelative -OutDir $coreLibDir -NativeObjectCacheRoot $nativeCache
if ($LASTEXITCODE -ne 0) { throw "core compilation failed" }

# The catalog is optional so this script can also build an image with no
# authored scenes; the guest then falls back to the scene built in code.
$catalogArguments = @{}
if ($CatalogRoot) {
    if (-not (Test-Path -LiteralPath $CatalogRoot)) {
        throw "The scene catalog directory does not exist: $CatalogRoot"
    }
    $catalogArguments["CatalogRoot"] = (Resolve-Path -LiteralPath $CatalogRoot).Path
}

$imageDir = "$sessionRelativeRoot/image-staging"
Write-Host "Linking the image with content from $contentRelative"
& (Join-Path $PSScriptRoot "build.ps1") -Config $Config -OutDir $imageDir `
    -CoreGenerated $generatedRelative -CoreLib "$coreLibDir/libhelcore.a" `
    -ContentRoot $contentRelative -Define $Define -NativeObjectCacheRoot $nativeCache @catalogArguments
if ($LASTEXITCODE -ne 0) { throw "image link failed" }

$built = Join-Path $repo "$imageDir/helengine.xex"
if (-not (Test-Path -LiteralPath $built)) { throw "the link step produced no image at $built" }
Copy-Item -LiteralPath $built -Destination (Join-Path $output "helengine.xex") -Force
Copy-Item -LiteralPath (Join-Path $repo "$imageDir/helengine.elf") -Destination (Join-Path $output "helengine.elf") -Force
Copy-Item -LiteralPath (Join-Path $repo "$imageDir/hashes.txt") -Destination (Join-Path $output "hashes.txt") -Force
Write-Host "Image written to $output/helengine.xex"
}
finally {
# The isolated Docker staging tree is deleted below. Retain the actual compiler
# diagnostics in the caller's output even when compilation or linking throws.
$sessionBuildLog = Join-Path $sessionRoot 'image-staging/build.log'
if (-not $ValidateScratchOnly -and (Test-Path -LiteralPath $sessionBuildLog -PathType Leaf)) {
    Copy-Item -LiteralPath $sessionBuildLog -Destination (Join-Path $output 'native-build.log') -Force
}
$resolvedSessionRoot = [IO.Path]::GetFullPath($sessionRoot)
$resolvedBuildRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $resolvedSessionRoot.StartsWith($resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Temporary X360 build root escaped the repository build directory: $resolvedSessionRoot"
}
if (-not $fixedScratch -and (Test-Path -LiteralPath $resolvedSessionRoot)) { Remove-Item -LiteralPath $resolvedSessionRoot -Recurse -Force }
if ($scratchLock) { $scratchLock.Dispose() }
}
