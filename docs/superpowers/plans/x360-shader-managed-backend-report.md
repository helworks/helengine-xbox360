# Phase 2e managed Xbox 360 shader backend report

## Outcome

Implemented the managed Xbox 360 shader backend, registry contribution, and
one-process-per-request bridge to the frozen Python host protocol. The bridge
maps the worker's actual Xenos bytecode/reflection and preserves host metadata,
tool/compile identities, and consumed-include provenance. It does not enable
legacy cook/render target selection, material activation, or a guest ABI.

## Verification

- Contract RED before implementation:
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --filter "FullyQualifiedName~Xbox360ShaderBackendTests"`
  failed to compile because the managed backend/registration API did not yet
  exist.
- Backend-focused GREEN during skeleton implementation:
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj --no-restore -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --filter "FullyQualifiedName~Xbox360ShaderBackendTests"`
  passed 7/7.
- Final process and real integration run:
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj --no-restore -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --filter "FullyQualifiedName~Xbox360ShaderCompilerProcessTests|FullyQualifiedName~Xbox360ShaderCompilerIntegrationTests"`
  passed 16/16 (13 process cases and 3 real shaderc/service integration cases).
- Full local builder test project:
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj --no-restore -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts`
  passed 31/31, 0 skipped.
- Default-output fixture-copy check (without `ArtifactsPath`):
  `dotnet build builder.tests/fixtures/Xbox360ShaderProtocolFixture/Xbox360ShaderProtocolFixture.csproj`
  succeeded; then
  `dotnet msbuild builder.tests/helengine.x360.builder.tests.csproj -target:CopyXbox360ShaderProtocolFixture -property:Configuration=Debug`
  copied the fixture DLL, EXE, deps, and runtimeconfig into the standard
  `builder.tests/bin/Debug/net9.0/shader-protocol-fixture` output directory.
- Real integration invokes the builder-bundled six-file Python tool set and
  the shaderc DLL at
  `C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll`.
  It verifies the transform VS and textured PS baseline SHA-256 values,
  reflected inputs/outputs and cbuffer members, physical texture/sampler pair
  versus request-shifted logical slots, alternate entrypoint/define, nested
  active includes, include-derived reflection and identities, changed include
  microcode, and failure followed by a fresh successful service compile.
- The Python worker's host-protocol suite was reported separately by the parent
  as 91/91 passing; it was not rerun or counted as part of this C# report.

## Follow-up: validate timeout before child launch

On 2026-09-23 (America/Sao_Paulo), review found that `TimeSpan.MaxValue` passes
the options' positive-duration check but is rejected by
`CancellationTokenSource.CancelAfter`. Previously the CTS was armed after
`Process.Start`, and that exception occurred outside the child-cleanup block.
The focused regression test observed the fixture's launch marker with that
interval (RED). The compiler now creates and arms its timeout CTS before
starting the process; the default remains 60 seconds and no upper bound was
added to options.

- Focused RED:
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj --no-restore -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --filter "FullyQualifiedName~Unrepresentable_timeout_fails_before_starting_the_protocol_child"`
  failed because the protocol fixture had started.
- Focused GREEN, same command after the fix: passed 1/1; the fixture launch
  marker was not created.
- Follow-up process and real integration suite:
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj --no-restore -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --filter "FullyQualifiedName~Xbox360ShaderCompilerProcessTests|FullyQualifiedName~Xbox360ShaderCompilerIntegrationTests"`
  passed 17/17 (14 process cases and 3 real integration cases).
- Full local test project after the follow-up:
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj --no-restore -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts`
  reported 31 passed, 1 failed. The sole failure is the concurrent shared
  contract assertion `ShaderSharedContractTests.Xbox360_is_the_appended_target_with_a_round_tripping_descriptor`
  at line 28: it expects 10 enum values but sees 9 descriptors. The Xbox360=8
  and PS3-ID assertions pass. Shared enum/test files were not changed. The
  earlier 31/31 result above remains the historical result for the earlier
  checkout state, before this concurrent enum/descriptor mismatch appeared.

## Changed paths

- `builder/Xbox360PlatformAssetBuilder.cs`
- `builder/helengine.x360.builder.csproj`
- `builder/shaders/Xbox360PlatformShaderBackendRegistration.cs`
- `builder/shaders/Xbox360ShaderBackend.cs`
- `builder/shaders/Xbox360ShaderCompilationException.cs`
- `builder/shaders/Xbox360ShaderCompileResult.cs`
- `builder/shaders/Xbox360ShaderCompiler.cs`
- `builder/shaders/Xbox360ShaderCompilerOptions.cs`
- `builder/shaders/Xbox360ShaderProtocolMapper.cs`
- `builder.tests/helengine.x360.builder.tests.csproj`
- `builder.tests/Xbox360ShaderBackendTests.cs`
- `builder.tests/Xbox360ShaderCompilerProcessTests.cs`
- `builder.tests/Xbox360ShaderCompilerIntegrationTests.cs`
- `builder.tests/fixtures/Xbox360ShaderProtocolFixture/Xbox360ShaderProtocolFixture.csproj`
- `builder.tests/fixtures/Xbox360ShaderProtocolFixture/Program.cs`
- `docs/x360-shader-backend.md`
- `docs/superpowers/plans/x360-shader-managed-backend-report.md`

## Limitations

Include resolution is a synchronous host callback and cannot itself be
preempted. Only explicit interface locations and the currently supported
resource kinds/layout are mapped; unsupported SPIR-V or lowering behavior is a
compile error. The returned Xbox-specific metadata remains host-only and is
not a guest ABI or cooker contract. No shared Engine or Python files were
modified for Phase 2e.

## Parent verification after delivery

The parent reran the complete local project and obtained 30 passed / 1 failed
(31 total, zero skipped). All 23 new backend/process/real-integration cases
passed. The existing descriptor-enumeration test failed because another task
added original-Xbox `ShaderCompileTarget.Xbox = 9` while the shared descriptor
table still contained only nine entries. A focused rerun reproduced the same
10-versus-9 mismatch. Xbox360 remains 8 and Ps3 remains 7. No shared file or
test expectation was changed to hide this concurrent inconsistency.

The standard builder build passed with 0 errors and 41 warnings in unchanged
shared-core files. The parent also ran the actual bundled Python compiler,
verified all six bundled tool hashes against current sources, and confirmed
the diagnostic XEX hash is unchanged. Final task review approved the timeout
follow-up. The parent's final rerun after that fix is 31 passed / 1 failed
(32 total, zero skipped), still only the same shared descriptor mismatch. Do not
interpret the earlier worker run as a claim that the current full suite is green.
