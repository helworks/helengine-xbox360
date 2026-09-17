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
    [string]$Config = "tools/config.local.json",
    [string[]]$Define = @("HEL_DEMO_MODE=0")
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$generated = (Resolve-Path -LiteralPath $GeneratedCoreRoot).Path
$output = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force $output | Out-Null

# Relative paths keep the docker mount and the build scripts consistent; the
# builder passes absolute ones, so convert them back to repository-relative.
function ConvertTo-RepoRelative([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if ($full.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($repo.Length).TrimStart('\', '/').Replace('\', '/')
    }
    throw "build-image.ps1 needs paths inside the repository; '$path' is outside '$repo'."
}

$generatedRelative = ConvertTo-RepoRelative $generated
$contentRelative = ConvertTo-RepoRelative $ContentRoot
$coreLibDir = "build/core"

Write-Host "Compiling the generated core from $generatedRelative"
& (Join-Path $PSScriptRoot "build-core.ps1") -Config $Config -GeneratedRoot $generatedRelative -OutDir $coreLibDir |
    Select-String "failed|archive" | ForEach-Object { Write-Host $_.Line }
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

$imageDir = "build/image-staging"
Write-Host "Linking the image with content from $contentRelative"
& (Join-Path $PSScriptRoot "build.ps1") -Config $Config -OutDir $imageDir `
    -CoreGenerated $generatedRelative -CoreLib "$coreLibDir/libhelcore.a" `
    -ContentRoot $contentRelative -Define $Define @catalogArguments |
    Select-String "content entries|Scene catalog|Built|failure" | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0) { throw "image link failed" }

$built = Join-Path $repo "$imageDir/helengine.xex"
if (-not (Test-Path -LiteralPath $built)) { throw "the link step produced no image at $built" }
Copy-Item -LiteralPath $built -Destination (Join-Path $output "helengine.xex") -Force
Copy-Item -LiteralPath (Join-Path $repo "$imageDir/helengine.elf") -Destination (Join-Path $output "helengine.elf") -Force
Copy-Item -LiteralPath (Join-Path $repo "$imageDir/hashes.txt") -Destination (Join-Path $output "hashes.txt") -Force
Write-Host "Image written to $output/helengine.xex"
