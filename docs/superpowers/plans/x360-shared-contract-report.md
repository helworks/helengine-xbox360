# Phase 2b shared shader contract report

Status: DONE_WITH_CONCERNS

## Changes

- Appended `ShaderCompileTarget.Xbox360 = 8`; the existing `Ps3` ordinal remains 7.
- Added the `x360` / `HEL_API_X360` descriptor.
- Added host-side `IShaderBackendCachePolicy.UseSharedCache`. `ShaderCompileService.Compile` now calls an opt-out backend immediately after backend resolution, before cache-key creation or cache access. Default and explicit-true backends retain existing cache behavior.
- Added `builder.tests/helengine.x360.builder.tests.csproj` and focused tests for target identity, the bypass contract, fresh request/include/options/compiler state, failure followed by success, normal cache hits, null requests, and unregistered targets.
- Parent made a mechanical `.gitignore` integration change for `builder.tests/bin/`, `builder.tests/obj/`, and `.artifacts/`; included in review scope, not authored here.

## TDD and verification

- Identity RED: `dotnet test builder.tests\helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:\dev\helworks\helengine-x360\.artifacts` — 2 expected failures (`Xbox360` missing and policy missing), 1 pass.
- Behavioral RED: the focused `--filter` run for opt-out, failed-result, default/true cache, and null/unregistered cases — 2 expected opt-out failures at `IShaderCompileCache.TryGet`, 3 passes.
- GREEN: `dotnet test builder.tests\helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:\dev\helworks\helengine-x360\.artifacts --no-restore` — 8 passed, 0 failed.
- Shared descriptors: `dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj -p:ArtifactsPath=C:\dev\helworks\helengine-x360\.artifacts --no-restore --no-build --filter "FullyQualifiedName~ShaderTargetDescriptorTests"` — 5 passed, 0 failed.
- Combined descriptor and boundary run compiled the shared editor tests and ran 7 tests: 6 passed, 1 failed. `ShaderProjectBoundaryTests.Shader_runtime_and_compilation_sources_have_distinct_project_boundaries` could not locate the Engine root because the test resolves it by walking upward from the assembly, while `ArtifactsPath` places that assembly under the x360 checkout. The other boundary test passed. No unrelated test/project changes were made.
- Build output was directed to the local `.artifacts` path to avoid denied writes to shared Engine `obj` directories. The first default-path build received `Access is denied`; the local-artifact build and tests completed. The shared editor build emitted existing analyzer/compiler warnings.

## Files in scope

Shared Engine checkout (`C:/dev/helworks/helengine`):

- `engine/helengine.shader/shaders/runtime/ShaderCompileTarget.cs`
- `engine/helengine.shader/shaders/runtime/ShaderTargetDescriptors.cs`
- `engine/helengine.shader/shaders/compilation/ShaderCompileService.cs`
- `engine/helengine.shader/shaders/compilation/IShaderBackendCachePolicy.cs` (new)

X360 checkout (`C:/dev/helworks/helengine-x360`):

- `builder.tests/helengine.x360.builder.tests.csproj` (new)
- `builder.tests/ShaderSharedContractTests.cs` (new)
- `.gitignore` (parent integration change described above)

The shared compilation project boundary was left intact; no Python/frontend files or existing builder production files were edited. Graph coverage cited in the brief was `metadata_changed` for the target, descriptor, service, and shared descriptor test paths, so source and current test results were used as evidence.

## Parent integration verification

The location-dependent boundary limitation is resolved without a source change:
parent ran the unchanged shared tests from the normal Engine output path with
approved access to generated obj/bin files:

`dotnet test C:/dev/helworks/helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --no-restore --filter 'FullyQualifiedName~ShaderTargetDescriptorTests|FullyQualifiedName~ShaderProjectBoundaryTests' --nologo --verbosity quiet`

Result: **7 passed, 0 failed, 0 skipped**, exit0. This includes both boundary
tests and all five descriptor tests. The shared build emitted existing warnings
in vendor/renderer/editor/test sources; no unrelated source was changed or warning
suppressed. The earlier redirected-path failure remains recorded above as history.
