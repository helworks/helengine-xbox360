[CmdletBinding()]
param([string]$GeneratedRoot = '.artifacts/cooker-runtime-generated-fatal/generated')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$generated = (Resolve-Path (Join-Path $repo $GeneratedRoot)).Path
$prefix = $repo.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $generated.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Generated root must be inside checkout.' }
$generatedRelative = $generated.Substring($prefix.Length).Replace('\', '/')
$output = Join-Path $repo ('.artifacts/shader-runtime/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
New-Item -ItemType Directory -Path $output | Out-Null
$image = 'devkitpro/devkitppc@sha256:db8e39c73546d461d5606dc7dfed462a7d5957498502562e3b46769d02496cc1'
$script = @'
set -euo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:$PATH
flags='-mcpu=powerpc -m32 -mbig-endian -mno-eabi -msdata=none -mno-altivec -mhard-float -mstrict-align -fno-builtin -O2 -Wall -Wextra -Werror'
sources=(/work/tests/shared_batch_writer_test.cpp)
for source in Batch2DWriter Batch2DBuffer Batch2DRun Batch2DVertex RuntimeTexture RuntimeData float4 float4x4; do
  sources+=(/work/$GENERATED/$source.cpp)
done
for sanitizer in none address undefined; do
  extra=()
  if [ "$sanitizer" != none ]; then extra+=(-fsanitize=$sanitizer); fi
  if [ "$sanitizer" = address ]; then extra+=(-fno-pie -no-pie); fi
  g++ -std=c++20 -O1 -ffunction-sections -fdata-sections -Wl,--gc-sections "${extra[@]}" -I/work/$GENERATED -I/work/src/platform/xenia_guest "${sources[@]}" -o /tmp/shared-writer-$sanitizer
  timeout 30s /tmp/shared-writer-$sanitizer > /out/shared-writer-$sanitizer.log 2>&1
  cat /out/shared-writer-$sanitizer.log
done
for source in mesh video x360_shader_runtime x360_shader_content; do
  powerpc-eabi-gcc $flags -ffreestanding -std=c11 -I/work/build/shaders -c /work/src/platform/xenia_guest/$source.c -o /out/$source.o
done
for source in X360Batch2D X360RenderManager2D X360MeshMaterial X360RenderManager3D; do
  powerpc-eabi-g++ $flags -Wno-error -std=c++20 -fno-exceptions -frtti -fno-threadsafe-statics -I/work/$GENERATED -I/work/src/platform/xenia_guest -c /work/src/platform/xenia_guest/$source.cpp -o /out/$source.o
done
echo 'PowerPC runtime and generated-core adapters compiled'
timeout 180s powerpc-eabi-g++ $flags -w -std=c++20 -fno-exceptions -frtti -fno-threadsafe-statics -I/work/$GENERATED -I/work/src/platform/xenia_guest -c /work/$GENERATED/generated_unity.cpp -o /out/generated-core.o
echo 'PowerPC generated core compiled'
'@
$encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($script))
$command = "printf '%s' '$encoded' | base64 -d | bash"
$ErrorActionPreference = 'Continue'
& docker run --pull=never --rm --network none --mount "type=bind,source=$repo,target=/work,readonly" --mount "type=bind,source=$output,target=/out" --env "GENERATED=$generatedRelative" $image bash -lc $command *> (Join-Path $output 'compile.log')
$status = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Get-Content (Join-Path $output 'compile.log') -Tail 50
Write-Host "Output: $output"
if ($status -ne 0) { throw "Runtime compilation failed: $status" }
