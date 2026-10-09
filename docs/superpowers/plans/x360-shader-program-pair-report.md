# Phase 3a implementation report: validated shader programs and pair linking

## Result

Implemented the Phase 3a host snapshot and vertex/pixel linker under the bounded
ownership in `x360-shader-program-pair-brief.md`. No existing shared/runtime,
compiler, Python, renderer, or project configuration files were changed.

## TDD and verification

- The first focused run was RED before the program/factory/pair types existed.
- Native duplicate-export regressions were added separately for duplicate VS
  varyings and duplicate PS colors. The `duplicate_native` filter was RED at
  2 failed (the factory silently accepted both); checking `HashSet.Add` and
  throwing contextual `InvalidDataException` made that same focused set GREEN
  at 2/2.
- A real pixel-only cbuffer fixture emitted `register_count: 0`. The contract
  requires 1..64, so production validation correctly rejects it. The pixel
  constant-bank test now uses the fixture's real reflection/uniform metadata
  with a controlled valid register header, isolating bank-base validation
  without weakening production checks.
- Final focused command:

  ```text
  dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --filter FullyQualifiedName~Xbox360ShaderProgram
  ```

  Exit code 0; 48 passed, 0 failed, 0 skipped.
- Final full-suite command:

  ```text
  dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore
  ```

  Exit code 0; 80 passed, 0 failed, 0 skipped.
- An earlier focused run before correcting the pixel-only fixture failed 1/48;
  after isolating the register header, the final focused and full runs above
  passed. No tests were weakened to accept register count zero.

## Files created

- `builder/shaders/Xbox360ShaderProgram.cs`
- `builder/shaders/Xbox360ShaderMetadataReader.cs`
- `builder/shaders/Xbox360ShaderProgramFactory.cs`
- `builder/shaders/Xbox360ShaderProgramPair.cs`
- `builder.tests/Xbox360ShaderProgramTests.cs`
- `builder.tests/Xbox360ShaderProgramPairTests.cs`
- `builder.tests/Xbox360ShaderProgramTestSupport.cs`
- `docs/x360-shader-program-layout.md`
- `docs/superpowers/plans/x360-shader-program-pair-report.md`

## Contract evidence and limitations

The parent evidence recorded in the brief uses the verify-tier code graph
snapshot generated `2026-09-24T00:00:54Z`, querying
`Xbox360ShaderCompileResult`, `lower_spirv`, and the shader probe. The relevant
paths were marked `metadata_changed`; `src/platform/xenia_guest/shader_gpu_probe.inc`
was `parse_unusable`. The brief records direct observations of compiler-result
construction, lowerer metadata/input/used-uniform/texture generation, and the
diagnostic-only GPU probe validator/cost/runtime upload path. That validator's
fixture restrictions were not reused.

The implementation intentionally validates the current supported compiler
contract rather than arbitrary Xenos programs or the full instruction set.
Uniform-only pixel metadata with a zero program register count remains rejected
by the brief's explicit 1..64 rule; whether that compiler output should be
normalized upstream is outside this task. Asset IDs, serialization/wire format,
binary reading, C/runtime integration, shader activation, and material selection
remain future work. Python sources were unchanged, so the Python suite was not
rerun. No commit was created.

### Pixel output semantic evidence

The brief's typed host interface is `LOCATION0 float4`, not `SV_TARGET0`.
The HLSL test fixtures declare their return semantic as `SV_Target0`, but the
existing protocol mapper reads reflected interfaces by SPIR-V `location` and
creates `ShaderVertexElement("LOCATION", index, format)` for numeric locations
(`builder/shaders/Xbox360ShaderProtocolMapper.cs`, `ReadInterfaces`). The real
textured-pixel compile test then observes the resulting program output as
`Xbox360ShaderInterfaceKind.Location`, index 0, width 4. Thus `SV_Target0` is
the source-language spelling; `LOCATION0` is the backend reflection/host
snapshot representation. The layout doc now states both sides of that mapping.
No shared reflection or mapper code was changed, and no build/test was run for
this documentation-only clarification.
