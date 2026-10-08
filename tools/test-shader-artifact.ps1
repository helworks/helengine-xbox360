[CmdletBinding()]
param(
    [ValidateSet('All', 'Envelope', 'Semantic', 'Integration')]
    [string]$Suite = 'All'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$artifactRoot = Join-Path $repoRoot '.artifacts\shader-artifact'
$image = 'devkitpro/devkitppc@sha256:db8e39c73546d461d5606dc7dfed462a7d5957498502562e3b46769d02496cc1'

if (-not (Test-Path -LiteralPath $artifactRoot)) {
    New-Item -ItemType Directory -Path $artifactRoot | Out-Null
}
$artifactRoot = (Resolve-Path -LiteralPath $artifactRoot).Path
$repoPrefix = $repoRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $artifactRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved artifact output escaped the checkout: $artifactRoot"
}

$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ', [Globalization.CultureInfo]::InvariantCulture)
$outputPath = Join-Path $artifactRoot $runId
if (Test-Path -LiteralPath $outputPath) {
    throw "Refusing to reuse existing artifact output directory: $outputPath"
}
New-Item -ItemType Directory -Path $outputPath | Out-Null
$outputPath = (Resolve-Path -LiteralPath $outputPath).Path
if (-not $outputPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved run output escaped the checkout: $outputPath"
}

$integrationOutput = Join-Path $outputPath 'integration'
$engineRoot = if ([string]::IsNullOrWhiteSpace($env:HELENGINE_ROOT)) {
    'C:\dev\helworks\helengine'
} else {
    [IO.Path]::GetFullPath($env:HELENGINE_ROOT)
}
$shaderc = 'C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll'
$exporterProject = Join-Path $repoRoot 'tests\shaders\artifact-export\ArtifactExport.csproj'
$needsIntegration = $Suite -in @('All', 'Integration')
if ($needsIntegration) {
    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw 'The .NET SDK is required for the real shader artifact integration suite.' }
    if (-not (Test-Path -LiteralPath $shaderc -PathType Leaf)) { throw "The required shaderc library is missing: $shaderc" }
    foreach ($source in @(
        (Join-Path $repoRoot 'tests\shaders\fixtures\transform_vs.hlsl'),
        (Join-Path $repoRoot 'tests\shaders\fixtures\texture_ps.hlsl'),
        (Join-Path $engineRoot 'engine\helengine.shader.compilation\helengine.shader.compilation.csproj'),
        (Join-Path $engineRoot 'engine\helengine.shader\shaders\SpriteBatchShaderVariants.cs'),
        (Join-Path $engineRoot 'engine\helengine.editor\shaders\builtin\SpriteBatchShader.hlsl'),
        $exporterProject
    )) {
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required integration source is missing: $source" }
    }
    & docker image inspect $image *> (Join-Path $outputPath 'docker-image-inspect.log')
    if ($LASTEXITCODE -ne 0) { throw "Required pinned Docker image is unavailable locally: $image" }

    New-Item -ItemType Directory -Path $integrationOutput | Out-Null
    $restoreLog = Join-Path $outputPath 'exporter-restore.log'
    & dotnet restore $exporterProject `
        "-p:ArtifactsPath=$repoRoot/.artifacts" "-p:HelengineRoot=$engineRoot" *> $restoreLog
    if ($LASTEXITCODE -ne 0) { Get-Content -Tail 80 $restoreLog; throw 'Exporter project restore failed.' }

    $exportLog = Join-Path $outputPath 'exporter-run.log'
    & dotnet run --project $exporterProject `
        "-p:ArtifactsPath=$repoRoot/.artifacts" "-p:HelengineRoot=$engineRoot" --no-restore -- `
        --output $integrationOutput --shaderc $shaderc --engine-root $engineRoot *> $exportLog
    if ($LASTEXITCODE -ne 0) { Get-Content -Tail 80 $exportLog; throw 'Real shader artifact exporter failed.' }
    foreach ($name in @('mesh.x3sp', 'batch-unoptimized.x3sp', 'batch-optimized.x3sp', 'expected.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $integrationOutput $name) -PathType Leaf)) {
            throw "Exporter succeeded without producing required output '$name'. See $exportLog"
        }
    }
    $actualNames = @(Get-ChildItem -LiteralPath $integrationOutput -File | Select-Object -ExpandProperty Name | Sort-Object)
    $requiredNames = @('batch-optimized.x3sp', 'batch-unoptimized.x3sp', 'expected.txt', 'mesh.x3sp')
    if (Compare-Object $requiredNames $actualNames) { throw "Exporter produced an unexpected output set in $integrationOutput" }
    $cookProject = Join-Path $repoRoot 'tests/shaders/cooker-export/CookerExport.csproj'
    & dotnet run --project $cookProject "-p:ArtifactsPath=$repoRoot/.artifacts" -- $outputPath $engineRoot $shaderc *> (Join-Path $outputPath 'cooker-export.log')
    if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $outputPath 'cooker-export.log') -Tail 50; throw 'Cooker export failed.' }
    & python (Join-Path $repoRoot 'tools/pack-content.py') --content-root (Join-Path $outputPath 'archive-staged') --out-bin (Join-Path $outputPath 'content.bin') --out-stub (Join-Path $outputPath 'content.S') --incbin-path /out/content.bin *> (Join-Path $outputPath 'pack.log')
    if ($LASTEXITCODE -ne 0) { throw 'Cooked content packing failed.' }
}

$sourceMount = $repoRoot.Replace('\', '/')
$outputMount = $outputPath.Replace('\', '/')
$containerScript = @'
set -euo pipefail
export PATH=/opt/devkitpro/devkitPPC/bin:$PATH
cd /work

echo "Host compiler: $(gcc --version | head -n 1)"
echo "PowerPC compiler: $(powerpc-eabi-gcc --version | head -n 1)"

test_names=()
if [ "$X3SP_SUITE" = All ] || [ "$X3SP_SUITE" = Envelope ]; then
  test_names+=(envelope)
fi
if [ "$X3SP_SUITE" = All ] || [ "$X3SP_SUITE" = Semantic ]; then
  test_names+=(semantic)
fi
if [ "$X3SP_SUITE" = All ] || [ "$X3SP_SUITE" = Integration ]; then
  test_names+=(integration)
  test_names+=(runtime)
  test_names+=(content)
fi

test_sources() {
  case "$1" in
    content)
      printf '%s\n' \
        /work/tests/shader_content_integration_test.c \
        /out/content.S \
        /work/src/platform/xenia_guest/content.c \
        /work/src/platform/xenia_guest/x360_shader_runtime.c \
        /work/src/platform/xenia_guest/x360_shader_content.c \
        /work/src/platform/xenia_guest/x360_shader_artifact.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_semantics.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_sha256.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_envelope.c
      ;;
    runtime)
      printf '%s\n' \
        /work/tests/shader_runtime_test.c \
        /work/src/platform/xenia_guest/mesh.c \
        /work/src/platform/xenia_guest/x360_shader_runtime.c \
        /work/src/platform/xenia_guest/x360_shader_content.c \
        /work/src/platform/xenia_guest/x360_shader_artifact.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_semantics.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_sha256.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_envelope.c
      ;;
    envelope)
      printf '%s\n' \
        /work/tests/shader_artifact_envelope_test.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_sha256.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_envelope.c
      ;;
    semantic)
      printf '%s\n' \
        /work/tests/shader_artifact_test.c \
        /work/src/platform/xenia_guest/x360_shader_artifact.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_semantics.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_sha256.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_envelope.c
      ;;
    integration)
      printf '%s\n' \
        /work/tests/shader_artifact_test.c \
        /work/src/platform/xenia_guest/x360_shader_artifact.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_semantics.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_sha256.c \
        /work/src/platform/xenia_guest/x360_shader_artifact_envelope.c
      ;;
    *) return 2 ;;
  esac
}

for test_name in "${test_names[@]}"; do
  mapfile -t sources < <(test_sources "$test_name")
  gcc -std=c11 -O1 -Wall -Wextra -Werror \
    -I/work/src/platform/xenia_guest \
    -o "/out/shader-artifact-$test_name-test" "${sources[@]}"
  test_log="/out/host-test-$test_name.log"
  set +e
  timeout 30s "/out/shader-artifact-$test_name-test" >"$test_log" 2>&1
  test_status=$?
  set -e
  if [ "$test_status" -ne 0 ]; then
    echo "host $test_name test FAILED (exit $test_status)"
    cat "$test_log"
    exit "$test_status"
  fi
  cat "$test_log"
done

run_sanitizer() {
  sanitizer="$1"
  flag="-fsanitize=$sanitizer"
  extra_flags=()
  if [ "$sanitizer" = address ]; then
    extra_flags=(-fno-pie -no-pie)
  fi
  probe="/tmp/shader-artifact-sanitizer-probe-$sanitizer"
  probe_log="/tmp/shader-artifact-sanitizer-probe-$sanitizer.log"
  saved_probe_log="/out/sanitizer-probe-$sanitizer.log"

  if ! printf '%s\n' 'int main(void) { return 0; }' | \
      gcc -std=c11 "$flag" "${extra_flags[@]}" -x c - -o "$probe" >"$probe_log" 2>&1; then
    echo "$sanitizer: UNAVAILABLE (compiler probe failed)"
    cp "$probe_log" "$saved_probe_log"
    sed -n '1,12p' "$probe_log"
    return 0
  fi

  set +e
  timeout 10s "$probe" >"$probe_log" 2>&1
  probe_status=$?
  set -e
  if [ "$probe_status" -ne 0 ]; then
    if [ "$probe_status" -eq 124 ]; then
      echo "$sanitizer: UNAVAILABLE (runtime probe timed out after 10 seconds)"
    else
      echo "$sanitizer: UNAVAILABLE (runtime probe exited $probe_status)"
    fi
    cp "$probe_log" "$saved_probe_log"
    sed -n '1,20p' "$probe_log"
    return 0
  fi
  cp "$probe_log" "$saved_probe_log"

  echo "$sanitizer: available; running selected harnesses"
  for test_name in "${test_names[@]}"; do
    mapfile -t sources < <(test_sources "$test_name")
    test_binary="/tmp/shader-artifact-$test_name-$sanitizer"
    test_log="/tmp/shader-artifact-test-$test_name-$sanitizer.log"
    saved_test_log="/out/sanitizer-test-$test_name-$sanitizer.log"
    gcc -std=c11 -O1 -g -Wall -Wextra -Werror "$flag" \
      "${extra_flags[@]}" -I/work/src/platform/xenia_guest \
      -o "$test_binary" "${sources[@]}"
    set +e
    timeout 30s "$test_binary" >"$test_log" 2>&1
    test_status=$?
    set -e
    cp "$test_log" "$saved_test_log"
    if [ "$test_status" -ne 0 ]; then
      echo "$sanitizer: $test_name harness FAILED (exit $test_status); log: $saved_test_log"
      tail -n 40 "$test_log"
      return "$test_status"
    fi
    cat "$test_log"
  done
}

run_sanitizer address
run_sanitizer undefined

powerpc-eabi-gcc -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -I/work/src/platform/xenia_guest -c \
  /work/src/platform/xenia_guest/x360_shader_artifact_sha256.c \
  -o /out/x360_shader_artifact_sha256.ppc.o
powerpc-eabi-gcc -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -I/work/src/platform/xenia_guest -c \
  /work/src/platform/xenia_guest/x360_shader_artifact_envelope.c \
  -o /out/x360_shader_artifact_envelope.ppc.o
powerpc-eabi-gcc -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -I/work/src/platform/xenia_guest -c \
  /work/src/platform/xenia_guest/x360_shader_artifact.c \
  -o /out/x360_shader_artifact.ppc.o
powerpc-eabi-gcc -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -I/work/src/platform/xenia_guest -c \
  /work/src/platform/xenia_guest/x360_shader_artifact_semantics.c \
  -o /out/x360_shader_artifact_semantics.ppc.o
echo "PowerPC freestanding objects compiled"
for source in x360_shader_runtime x360_shader_content; do
  powerpc-eabi-gcc -std=c11 -Wall -Wextra -Werror -ffreestanding \
    -I/work/src/platform/xenia_guest -c /work/src/platform/xenia_guest/$source.c \
    -o /out/$source.ppc.o
done
'@
$containerScriptBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($containerScript))
$containerCommand = "printf '%s' '$containerScriptBase64' | base64 -d | bash"
$integrationEnvironment = @()
if ($needsIntegration) { $integrationEnvironment = @('--env', 'X3SP_INTEGRATION=1') }

Write-Host "Suite: $Suite"
Write-Host "Output: $outputPath"
& docker run --pull=never --rm --network none `
    --mount "type=bind,source=$sourceMount,target=/work,readonly" `
    --mount "type=bind,source=$outputMount,target=/out" `
    --env "X3SP_SUITE=$Suite" `
    @integrationEnvironment `
    $image bash -lc $containerCommand
if ($LASTEXITCODE -ne 0) {
    throw "Shader artifact suite '$Suite' failed with exit code $LASTEXITCODE"
}
