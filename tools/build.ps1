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
    [string]$ContentRoot = "build/content-staging",
    # Directory holding the scene catalog the platform builder generated. When
    # it is empty the stub catalog is linked instead and the image falls back
    # to the scene built in code.
    [string]$CatalogRoot = "",
    [string]$NativeObjectCacheRoot = "",
    [string[]]$Define = @()
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$nativeCacheRoot = if ([string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) { Join-Path $out 'native-cache' } else { [IO.Path]::GetFullPath($NativeObjectCacheRoot) }
$objectCacheRoot = Join-Path $nativeCacheRoot 'x360-image-objects'
New-Item -ItemType Directory -Force $objectCacheRoot | Out-Null
$outRel = $OutDir.Replace("\", "/")
$coreGenRel = $CoreGenerated.Replace("\", "/")
$coreLibRel = $CoreLib.Replace("\", "/")
if (-not (Test-Path (Join-Path $repo $CoreLib))) {
    throw "No core archive at $CoreLib. Run tools/build-core.ps1 first."
}

& (Join-Path $PSScriptRoot "build-shaders.ps1") -Config $Config | Out-Null
if ($LASTEXITCODE -ne 0) { throw "shader build failed" }

# Cooked content travels inside the image; with nothing staged this writes an
# empty archive so the image always links the same way.
$contentFull = Join-Path $repo $ContentRoot
# Capture rather than pipe: piping into Select-Object -First closes the
# pipeline early, which kills the packer and reports a false failure.
$contentLog = & $cfg.python (Join-Path $repo "tools/pack-content.py") `
    --content-root $contentFull `
    --out-bin (Join-Path $out "content.bin") `
    --out-stub (Join-Path $out "content.S") `
    --incbin-path "/work/$outRel/content.bin"
if ($LASTEXITCODE -ne 0) { $contentLog; throw "content packing failed" }
$contentLog | Select-Object -First 2 | ForEach-Object { Write-Host $_ }

# The scene catalog. Exactly one definition is linked: the builder's generated
# one when a build supplied it, otherwise the stub. It is copied into the
# output directory because the generated file lives in the editor's build
# workspace, outside the tree the container mounts.
$catalogSource = "src/demo/scene_catalog_stub.cpp"
if ($CatalogRoot) {
    $generatedCatalog = Join-Path $CatalogRoot "xbox360_runtime_scene_catalog.cpp"
    if (-not (Test-Path -LiteralPath $generatedCatalog)) {
        throw "A catalog root was given but holds no xbox360_runtime_scene_catalog.cpp: $CatalogRoot"
    }
    Copy-Item -LiteralPath $generatedCatalog -Destination (Join-Path $out "scene_catalog_generated.cpp") -Force
    $catalogSource = "$outRel/scene_catalog_generated.cpp"
    Write-Host "Scene catalog: generated, from $CatalogRoot"
} else {
    Write-Host "Scene catalog: stub (no authored scenes)"
}

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
    "src/platform/xenia_guest/X360InputBackend.cpp",
    "src/platform/xenia_guest/X360Math.cpp",
    "src/platform/xenia_guest/X360LibcShim.cpp",
    "src/platform/xenia_guest/X360RenderManager2D.cpp",
    "src/platform/xenia_guest/X360Batch2D.cpp",
    "src/platform/xenia_guest/X360MeshMaterial.cpp",
    "src/platform/xenia_guest/X360RenderManager3D.cpp",
    "src/demo/core_boot.cpp",
    "src/demo/core_scene.cpp",
    "src/demo/endian_test.cpp",
    "src/demo/matrix_test.cpp"
)
$sources = @(
    "src/platform/xenia_guest/start.S",
    "src/platform/xenia_guest/imports.S",
    "src/platform/xenia_guest/platform.c",
    "src/platform/xenia_guest/runtime.c",
    "src/platform/xenia_guest/heap.c",
    "src/platform/xenia_guest/content.c",
    "src/platform/xenia_guest/cxx_runtime.cpp",
    "src/platform/xenia_guest/video.c",
    "src/platform/xenia_guest/shader.c",
    "src/platform/xenia_guest/x360_shader_artifact.c",
    "src/platform/xenia_guest/x360_shader_artifact_envelope.c",
    "src/platform/xenia_guest/x360_shader_artifact_semantics.c",
    "src/platform/xenia_guest/x360_shader_artifact_sha256.c",
    "src/platform/xenia_guest/x360_shader_runtime.c",
    "src/platform/xenia_guest/x360_shader_content.c",
    "src/platform/xenia_guest/mesh.c",
    "src/platform/xenia_guest/texture.c",
    "src/platform/xenia_guest/x360_native_texture.c",
    "src/engine/frame.c",
    "src/engine/math.c",
    "src/demo/triangle.c",
    "src/demo/cube.c",
    "src/demo/cube_demo.c",
    "src/demo/content_test.c",
    "src/demo/cxx_selftest.cpp",
    "src/demo/main.c",
    $catalogSource
)
$compileLines = @()
$objects = @()
$objectNames = @()
foreach ($s in ($sources + $coreSources)) {
    if ($s -eq $catalogSource) {
        $objectName = if ($s.EndsWith('scene_catalog_generated.cpp')) { 'scene_catalog_generated.cpp.o' } else { 'scene_catalog_stub.cpp.o' }
    } else {
        $objectName = ($s -replace "[/\\]", "_") + ".o"
    }
    $objectNames += $objectName
    $obj = "`$OBJ_ROOT/$objectName"
    $objects += $obj
    if ($coreSources -contains $s) {
        $compileLines += "powerpc-eabi-g++ $coreCxxflags -c /work/$s -o $obj"
    } elseif ($s -eq $catalogSource) {
        # The generated catalog is compiled from the output directory, so it
        # needs to be told where the declarations it defines live.
        $compileLines += "powerpc-eabi-g++ $cxxflags -I/work/src/demo -c /work/$s -o $obj"
    } elseif ($s.EndsWith(".cpp")) {
        $compileLines += "powerpc-eabi-g++ $cxxflags -c /work/$s -o $obj"
    } else {
        $compileLines += "powerpc-eabi-gcc $cflags -c /work/$s -o $obj"
    }
}
# The content stub lives in the output directory rather than the source tree,
# because its archive is a build product.
$contentObjectName = 'content.S.o'
$contentObject = "`$OBJ_ROOT/$contentObjectName"
$objects += $contentObject

# The image pipeline has no compiler depfiles. Fingerprint every compile input
# and toolchain/flag choice, then validate cached object hashes before reusing
# the complete object set. Content is keyed separately because it is embedded
# through content.S and should only refresh that one object when cooked files
# change.
$toolchainId = (& docker image inspect $cfg.docker_image --format '{{.Id}}' | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($toolchainId)) { throw "Could not identify toolchain image $($cfg.docker_image)." }
$sourceInputs = @(
    (Get-ChildItem -LiteralPath (Join-Path $repo 'src') -File -Recurse)
    (Get-ChildItem -LiteralPath (Join-Path $repo 'build/shaders') -File -Recurse -ErrorAction SilentlyContinue)
    (Get-ChildItem -LiteralPath (Join-Path $repo $CoreGenerated) -File -Recurse)
    (Get-Item -LiteralPath (Join-Path $repo 'tools/build.ps1'))
    (Get-Item -LiteralPath (Join-Path $repo 'tools/build-shaders.ps1'))
)
$coreObjectCachePath = Join-Path $nativeCacheRoot 'x360-core/obj/helcore.o'
if (Test-Path -LiteralPath $coreObjectCachePath) {
    $sourceInputs += Get-Item -LiteralPath $coreObjectCachePath
} else {
    $sourceInputs += Get-Item -LiteralPath (Join-Path $repo $CoreLib)
}
if ($CatalogRoot) { $sourceInputs += Get-Item -LiteralPath (Join-Path $out 'scene_catalog_generated.cpp') }
$coreGeneratedFull = [IO.Path]::GetFullPath((Join-Path $repo $CoreGenerated))
$coreGeneratedPrefix = $coreGeneratedFull.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$coreLibraryFull = [IO.Path]::GetFullPath((Join-Path $repo $CoreLib))
$inputRecords = @(
    foreach ($input in $sourceInputs | Sort-Object FullName -Unique) {
        $fullInput = [IO.Path]::GetFullPath($input.FullName)
        $relativeInput = if ($fullInput.Equals([IO.Path]::GetFullPath($coreObjectCachePath), [StringComparison]::OrdinalIgnoreCase)) {
            'generated-core-object/helcore.o'
        } elseif ($fullInput.Equals($coreLibraryFull, [StringComparison]::OrdinalIgnoreCase)) {
            'generated-core-archive/libhelcore.a'
        } elseif ($fullInput.StartsWith($coreGeneratedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            'generated-core/' + $fullInput.Substring($coreGeneratedPrefix.Length).Replace('\', '/')
        } elseif ($fullInput.EndsWith('scene_catalog_generated.cpp', [StringComparison]::OrdinalIgnoreCase)) {
            'generated-scene-catalog.cpp'
        } elseif ($fullInput.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
            $fullInput.Substring($repo.Length).TrimStart('\', '/')
        } else {
            throw "A cache input is outside the repository and generated-core root: $fullInput"
        }
        "$relativeInput=$((Get-FileHash -LiteralPath $fullInput -Algorithm SHA256).Hash)"
    }
)
$coreCxxflagsForFingerprint = $coreCxxflags -replace [regex]::Escape("/work/$coreGenRel"), '/work/generated-core'
$nativeFingerprintText = @(
    $toolchainId, $cfg.docker_image, $common, $cflags, $cxxflags, $coreCxxflagsForFingerprint,
    'powerpc-eabi-g++ -nostartfiles -T src/platform/xenia_guest/link.ld -lstdc++ -lsupc++ -lm -lgcc',
    ($Define -join "`n"), ($inputRecords -join "`n"), ($objectNames -join "`n")
) -join "`n"
$fingerprintInputPath = Join-Path $objectCacheRoot 'native-fingerprint-input.txt'
if (Test-Path -LiteralPath $fingerprintInputPath) {
    $previousInputLines = @(Get-Content -LiteralPath $fingerprintInputPath)
    $currentInputLines = @($nativeFingerprintText -split "`n")
    $inputDifferences = @(Compare-Object -ReferenceObject $previousInputLines -DifferenceObject $currentInputLines)
    if ($inputDifferences.Count -gt 0) {
        Write-Host 'Native cache input differences:'
        $inputDifferences | Select-Object -First 8 | ForEach-Object { Write-Host ("{0} {1}" -f $_.SideIndicator, $_.InputObject) }
    }
}
Set-Content -LiteralPath $fingerprintInputPath -Value ($nativeFingerprintText -split "`n") -Encoding UTF8
$sha256 = [Security.Cryptography.SHA256]::Create()
$nativeFingerprint = ([BitConverter]::ToString($sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes($nativeFingerprintText)))).Replace('-', '').ToLowerInvariant()
$nativeFingerprintPath = Join-Path $objectCacheRoot 'native-fingerprint.txt'
$objectManifestPath = Join-Path $objectCacheRoot 'objects.json'
$nativeCacheHit = $false
$nativeFingerprintPrevious = if (Test-Path -LiteralPath $nativeFingerprintPath) { (Get-Content -LiteralPath $nativeFingerprintPath -Raw).Trim() } else { '' }
if ((Test-Path -LiteralPath $nativeFingerprintPath) -and (Test-Path -LiteralPath $objectManifestPath) -and
    $nativeFingerprintPrevious -eq $nativeFingerprint) {
    $manifest = Get-Content -LiteralPath $objectManifestPath -Raw | ConvertFrom-Json
    $nativeCacheHit = $true
    foreach ($objectName in $objectNames) {
        $cachedObject = Join-Path $objectCacheRoot $objectName
        $record = $manifest.objects | Where-Object { $_.name -eq $objectName } | Select-Object -First 1
        if (-not $record -or -not (Test-Path -LiteralPath $cachedObject) -or
            (Get-FileHash -LiteralPath $cachedObject -Algorithm SHA256).Hash -ne $record.sha256) {
            Write-Host "Object cache entry failed validation: $objectName"
            $nativeCacheHit = $false
            break
        }
    }
}
if (-not $nativeCacheHit) { Write-Host "Native cache fingerprint mismatch: current $nativeFingerprint; cached $nativeFingerprintPrevious" }
if (-not $nativeCacheHit) {
    Get-ChildItem -LiteralPath $objectCacheRoot -File -Filter '*.o' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne $contentObjectName } | Remove-Item -Force
}
$contentFingerprint = ([BitConverter]::ToString($sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes(
    "$toolchainId`n$cflags`n$((Get-FileHash -LiteralPath (Join-Path $out 'content.bin') -Algorithm SHA256).Hash)")))).Replace('-', '').ToLowerInvariant()
$contentFingerprintPath = Join-Path $objectCacheRoot 'content-fingerprint.txt'
$contentObjectPath = Join-Path $objectCacheRoot $contentObjectName
$contentObjectHashPath = Join-Path $objectCacheRoot 'content-object.sha256'
$contentCacheHit = (Test-Path -LiteralPath $contentFingerprintPath) -and
    (Get-Content -LiteralPath $contentFingerprintPath -Raw).Trim() -eq $contentFingerprint -and
    (Test-Path -LiteralPath $contentObjectPath) -and (Test-Path -LiteralPath $contentObjectHashPath) -and
    (Get-FileHash -LiteralPath $contentObjectPath -Algorithm SHA256).Hash -eq (Get-Content -LiteralPath $contentObjectHashPath -Raw).Trim()
if (-not $contentCacheHit -and (Test-Path -LiteralPath $contentObjectPath)) { Remove-Item -LiteralPath $contentObjectPath -Force }
Write-Host "Native object cache: $(if ($nativeCacheHit) { 'hit' } else { 'miss' }); content object cache: $(if ($contentCacheHit) { 'hit' } else { 'miss' })"
$nativeCompileLines = if ($nativeCacheHit) { 'echo "reused cached X360 native objects"' } else { $compileLines -join "`n" }
$contentCompileLine = if ($contentCacheHit) { 'echo "reused cached content object"' } else { "powerpc-eabi-gcc $cflags -c `$OUT/content.S -o $contentObject" }
$script = @"
set -euo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:`$PATH
OUT=/work/$outRel
OBJ_ROOT=/nativecache/x360-image-objects
mkdir -p `$OBJ_ROOT
powerpc-eabi-gcc --version | head -1 > `$OUT/toolchain.txt
powerpc-eabi-g++ --version | head -1 >> `$OUT/toolchain.txt
powerpc-eabi-ld --version | head -1 >> `$OUT/toolchain.txt
$nativeCompileLines
$contentCompileLine
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
& cmd /c "docker run --pull=never --rm --network none -v ""${repo}:/work"" -v ""${nativeCacheRoot}:/nativecache"" $($cfg.docker_image) bash ""/work/$outRel/build.sh"" > ""$buildLog"" 2>&1"
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
if (-not $nativeCacheHit) {
    $objectRecords = @(
        foreach ($objectName in $objectNames) {
            $cachedObject = Join-Path $objectCacheRoot $objectName
            if (-not (Test-Path -LiteralPath $cachedObject)) { throw "Native build did not produce cached object $cachedObject" }
            [ordered]@{ name = $objectName; sha256 = (Get-FileHash -LiteralPath $cachedObject -Algorithm SHA256).Hash }
        }
    )
    $manifestJson = [ordered]@{ schema = 1; fingerprint = $nativeFingerprint; objects = $objectRecords } | ConvertTo-Json -Depth 4
    Set-Content -LiteralPath $objectManifestPath -Value $manifestJson -Encoding UTF8
    Set-Content -LiteralPath $nativeFingerprintPath -Value $nativeFingerprint -Encoding ASCII
}
Set-Content -LiteralPath $contentFingerprintPath -Value $contentFingerprint -Encoding ASCII
Set-Content -LiteralPath $contentObjectHashPath -Value (Get-FileHash -LiteralPath $contentObjectPath -Algorithm SHA256).Hash -Encoding ASCII
