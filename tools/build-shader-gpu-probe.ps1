<# Builds the opt-in diagnostic image with the real authored-scene inputs. #>
param(
    [string]$Config = "tools/config.local.json",
    [string]$Shaderc = "",
    [string]$CoreGenerated = "build/from-editor/generated-core",
    [string]$CoreLib = "build/resume-authored-core/libhelcore.a",
    [string]$ContentRoot = "build/from-editor/content",
    [string]$CatalogRoot = "build/resume-authored-catalog"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outputRoot = Join-Path $repo "build/shader-gpu-probe"
$imageRelative = "build/shader-gpu-probe/image"
$imageRoot = Join-Path $repo $imageRelative
$xexPath = Join-Path $imageRoot "helengine.xex"

# An old image must not look usable if this diagnostic's inputs stop building.
if (Test-Path -LiteralPath $xexPath) {
    Remove-Item -LiteralPath $xexPath -Force
}

try {
    $cfg = Get-Content -LiteralPath (Join-Path $repo $Config) -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($Shaderc)) {
        $Shaderc = $env:HEL_SHADERC_DLL
    }
    if ([string]::IsNullOrWhiteSpace($Shaderc)) {
        throw "Pass -Shaderc or set HEL_SHADERC_DLL to shaderc_shared.dll"
    }
    $Shaderc = [IO.Path]::GetFullPath($Shaderc)
    if (-not (Test-Path -LiteralPath $Shaderc -PathType Leaf)) {
        throw "Shaderc DLL not found: $Shaderc"
    }

    $generator = Join-Path $repo "tools/shader-probe/gpu_probe.py"
    $vertex = Join-Path $repo "tests/shaders/fixtures/transform_vs.hlsl"
    $pixel = Join-Path $repo "tests/shaders/fixtures/texture_ps.hlsl"
    $vertexLayout = Join-Path $repo "tests/shaders/fixtures/gpu_probe_vs_layout.json"
    $pixelLayout = Join-Path $repo "tests/shaders/fixtures/texture_runtime_layout.json"

    & $cfg.python $generator `
        --vertex $vertex `
        --pixel $pixel `
        --vertex-layout $vertexLayout `
        --pixel-layout $pixelLayout `
        --shaderc $Shaderc `
        --output-dir $outputRoot
    if ($LASTEXITCODE -ne 0) {
        throw "GPU probe shader generation failed with exit code $LASTEXITCODE"
    }

    $headerPath = Join-Path $outputRoot "probe_shaders.h"
    $manifestPath = Join-Path $outputRoot "probe_manifest.json"
    foreach ($required in @($headerPath, $manifestPath,
            (Join-Path $outputRoot "vertex.bin"),
            (Join-Path $outputRoot "pixel.bin"))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "GPU probe generator omitted required output: $required"
        }
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $headerHash = (Get-FileHash -LiteralPath $headerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $expectedHeaderHash = [string]$manifest.hashes.header
    if ($headerHash -ne $expectedHeaderHash) {
        throw "Generated header hash does not match probe manifest"
    }
    $cacheRoot = Join-Path $outputRoot "native-cache/$headerHash"

    & (Join-Path $PSScriptRoot "build.ps1") `
        -Config $Config `
        -OutDir $imageRelative `
        -CoreGenerated $CoreGenerated `
        -CoreLib $CoreLib `
        -ContentRoot $ContentRoot `
        -CatalogRoot $CatalogRoot `
        -NativeObjectCacheRoot $cacheRoot `
        -Define @("HEL_SHADER_GPU_PROBE=1", "HEL_DEMO_MODE=0", "HEL_CORE_BOOT=1")
    if ($LASTEXITCODE -ne 0) {
        throw "GPU probe image build failed with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $xexPath -PathType Leaf)) {
        throw "GPU probe build returned without producing $xexPath"
    }

    $hashes = [ordered]@{
        xex = (Get-FileHash -LiteralPath $xexPath -Algorithm SHA256).Hash.ToLowerInvariant()
        header = $headerHash
        vertex_program = (Get-FileHash -LiteralPath (Join-Path $outputRoot "vertex.bin") -Algorithm SHA256).Hash.ToLowerInvariant()
        pixel_program = (Get-FileHash -LiteralPath (Join-Path $outputRoot "pixel.bin") -Algorithm SHA256).Hash.ToLowerInvariant()
        probe_id = [string]$manifest.probe_id
        upload_dwords = [int]$manifest.upload_dwords
    }
    $reproduction = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build-shader-gpu-probe.ps1 -Config `"$Config`" -Shaderc `"$Shaderc`" -CoreGenerated `"$CoreGenerated`" -CoreLib `"$CoreLib`" -ContentRoot `"$ContentRoot`" -CatalogRoot `"$CatalogRoot`""
    $hashLines = ($hashes.GetEnumerator() | ForEach-Object {
        "{0}: {1}" -f $_.Key, $_.Value
    }) -join "`r`n"
    $record = @(
        "Diagnostic shader GPU probe build",
        "",
        $hashLines,
        "",
        "Reproduction command:",
        $reproduction,
        ""
    ) -join "`r`n"
    Set-Content -LiteralPath (Join-Path $outputRoot "build-record.txt") -Value $record -Encoding UTF8
    Write-Output $record
} catch {
    if (Test-Path -LiteralPath $xexPath) {
        Remove-Item -LiteralPath $xexPath -Force
    }
    throw
}
