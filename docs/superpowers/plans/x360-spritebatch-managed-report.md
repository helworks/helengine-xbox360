# Task3 — managed Batch2D boundary report

**Status: DONE**

## Result

Added an explicit local `Xbox360ShaderVertexLayout` profile that defaults to
`Mesh3D` and can be set to `Batch2D`. The compiler validates the enum before
starting a process, sends `mesh3d-v1` or `batch2d-v1`, and only accepts reply
metadata whose required `vertex_layout` matches the locally selected profile.
The typed compile result and immutable program snapshot retain that selection.
The factory validates each active vertex fetch against the selected profile;
pixel varying validation remains independent of vertex fetch widths.

The shared engine request types and `IShaderBackend` were not changed. No
Python semantics, shared engine sources, PS3 sources, runtime files, emulator,
or UI were touched. The builder project file change adds only the copy item for
the existing `vertex_layout.py`.

## TDD evidence

The first focused RED run failed at compile time on the new tests' missing
`Xbox360ShaderVertexLayout`, compiler option, and snapshot property. The first
real-shader run then demonstrated that shaderc reports only active Textured VS
inputs 0–2; inputs 3–5 are unused by this entry point and do not appear in its
native input map. The checks now validate every reported fetch against the
six-location Batch2D contract and separately reject malformed fetch metadata,
widths, and out-of-profile locations. A complete six-location factory case
then exposed the earlier vertex-interface range guard still capped at
LOCATION3; the guard now follows the selected profile, and the six-location
case passes.

Focused GREEN command:

```text
dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --filter 'FullyQualifiedName~Xbox360SpriteBatchShaderTests|FullyQualifiedName~Batch2d_profile|FullyQualifiedName~vertex_layout'
Passed: 16, Failed: 0, Skipped: 0
```

Full suite:

```text
dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore
Passed: 95, Failed: 0, Skipped: 0
```

The known intermittent timeout test passed in the full run. No Python suite was
rerun because Task2 had already passed it and no Python file was changed.

## Real shared Textured evidence

Source read directly from the unchanged shared checkout:

```text
C:/dev/helworks/helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl
SHA-256: 3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2
```

The test selects `SpriteBatchShaderVariants.All` entry `Textured` and uses its
`VS`/`PS` entrypoints and empty define list. The actual `vertex_layout.py`
bundled source hash is `CFBF9984C976BD4F978F21A61DE1AC4354CC173A17E6E5EE76EC7AB956812E0A`.

With optimization both disabled and enabled, the shared Batch2D pair produced
the same binaries and these observed values:

| Evidence | Vertex | Pixel |
| --- | ---: | ---: |
| Microcode dwords | 45 | 15 |
| Register count | 5 | 4 |
| SHA-256 | `9cd3526ecd7a2016a89cabb10b1388979c58a6f62d24785d0e4206234b32882e` | `7c67ad650636cd7ff621c85568792ded67a3f2120a0dd3975fa78b60b22e849c` |
| Constant buffer | 64 bytes / 4 vec4 registers | none |

The vertex stage's active native inputs are locations 0–2, each `float4`,
`vf0`, 24-float stride, at offsets 0/4/8. The selected Batch2D profile defines
locations 0–5 as `float4` at offsets 0/4/8/12/16/20 with that same stride. The
varyings use locations and native slots 0 and 1 with widths 2 and 4. The pixel
texture/sampler pair maps to `tf1`.

The pair cost was derived from the observed word and constant counts:

```text
ProgramControl         = 0x00100304
ProgramUploadDwords    = 45 + 15 + 6 = 66
ConstantUploadDwords   = 4 * 4 + 2 = 18
StateUploadDwords      = 10
PairUploadDwords       = 66 + 18 + 10 = 94
```

The unchanged legacy binary assertions also passed:

```text
Mesh3D transform VS: 40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF
Texture PS:           D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A
```

## Files changed for Task3

- `builder/shaders/Xbox360ShaderVertexLayout.cs` — new profiles and protocol IDs.
- `builder/shaders/Xbox360ShaderCompilerOptions.cs` — defaulted, validated local selection.
- `builder/shaders/Xbox360ShaderCompiler.cs` — includes selection in the request.
- `builder/shaders/Xbox360ShaderProtocolMapper.cs` — requires and matches reply profile.
- `builder/shaders/Xbox360ShaderCompileResult.cs` — retains selected profile.
- `builder/shaders/Xbox360ShaderProgram.cs` — exposes immutable profile on snapshot.
- `builder/shaders/Xbox360ShaderProgramFactory.cs` — profile-specific fetch validation.
- `builder/helengine.x360.builder.csproj` — adds only the `vertex_layout.py` copy item.
- `builder.tests/Xbox360SpriteBatchShaderTests.cs` — new shared-source integration and mutation coverage.
- `builder.tests/Xbox360ShaderCompilerProcessTests.cs` — profile request/default and malformed-reply cases.
- `builder.tests/fixtures/Xbox360ShaderProtocolFixture/Program.cs` — echoes requested profile and has corruption modes.
- `builder.tests/Xbox360ShaderCompilerIntegrationTests.cs` — expects bundled `vertex_layout.py`.
- `builder.tests/Xbox360ShaderProgramTestSupport.cs` — retains selected profile when cloning and expects bundled file.
- `docs/x360-shader-program-layout.md` — documents profile and real pair facts.
- `docs/superpowers/plans/x360-spritebatch-managed-report.md` — this evidence and handoff report.

`git diff --check` reported no whitespace errors; Git printed line-ending
normalization notices for dirty checkout files. Existing unrelated checkout
changes were preserved. This is host-side compile and metadata evidence only;
there is no GPU upload, emulator run, runtime activation, or rendered-output
proof.
