<# Builds and runs the focused shader GPU probe ring harness in the pinned Docker image. #>
param(
    [string]$Config = "tools/config.local.json",
    [string]$OutDir = "build/host-shader-gpu-runtime"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content -LiteralPath (Join-Path $repo $Config) -Raw | ConvertFrom-Json
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
$outRelative = $OutDir.Replace("\", "/")
$script = @"
set -euo pipefail
mkdir -p /work/$outRelative
gcc -std=c11 -O1 -Wall -Wextra -Werror -I/work/build/shaders -o /work/$outRelative/runtime_test /work/tests/shader_gpu_runtime_test.c
/work/$outRelative/runtime_test
gcc -std=c11 -O1 -Wall -Wextra -Werror -DHEL_TEST_INVALID_VS=1 -DHEL_TEST_BAD_OPCODE=1 -I/work/build/shaders -o /work/$outRelative/invalid_test /work/tests/shader_gpu_runtime_test.c
/work/$outRelative/invalid_test
gcc -std=c11 -O1 -Wall -Wextra -Werror -DHEL_TEST_ZERO_CONSTANTS=1 -I/work/build/shaders -o /work/$outRelative/zero_constants_test /work/tests/shader_gpu_runtime_test.c
/work/$outRelative/zero_constants_test
"@
& docker run --rm -v "${repo}:/work" -w /work $cfg.docker_image bash -lc $script
if ($LASTEXITCODE -ne 0) { throw "shader GPU runtime host tests failed with exit code $LASTEXITCODE" }
Write-Host "SHADER GPU RUNTIME HOST TESTS: all passed"
