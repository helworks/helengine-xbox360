# Shared SpriteBatch Textured — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Preserve the user's Luna execution method. User approved this plan and the design amendment; implementation is underway.

**Goal:** Compile and validate the unmodified shared SpriteBatch Textured VS/PS through the Xbox 360 host pipeline while preserving the existing mesh route.

**Status (2026-09-25):** All three tasks implemented and task-scoped reviews passed.
Parent verification:95/95 .NET,108/108 Python. Detailed acceptance evidence and
the bounded matrix-lowering ruling are in `x360-spritebatch-progress.md` and
`x360-spritebatch-task-reviews.md`. No commit, runtime activation or GPU claim.

**Architecture:** Explicit Xbox 360 compiler layout profiles select physical vertex fetch metadata. Shared shader/catalog/core remain read-only. Optimized compilation retains the source-derived reflection names needed by the existing host contract; no guessed names or silent optimization fallback.

**Tech Stack:** Python/shaderc/SPIR-V/Xenos; C#/.NET9; existing real-compiler Python and .NET tests.

**Spec:** `../specs/2026-09-25-x360-shared-spritebatch-amendment.md` (approved).

## Global Constraints

- Existing checkout only; no worktrees, commits, emulator, capture or foreground control.
- Preserve unrelated dirty files and other agents' work. Shared Engine and PS3 sources are read-only.
- Reuse SpriteBatchShader.hlsl and SpriteBatchShaderVariants.All, never copy the shader or duplicate its catalog.
- Keep legacy mesh vf0, stride12 dwords and offsets unchanged. Batch2D uses stride24 dwords with six float4 fields at offsets0,4,8,12,16,20.
- Select layout explicitly, never by filename, program name or incidental active input subset.
- Preserve tf1/vf0 separation and separate VS/PS constant banks.
- No RoundedShape support claim, artifact format freeze, cooker activation or renderer changes in this first slice.
- Any whole-branch independent review still requires user authorization. Task-scoped review only.

## Review Focus

1. Sparse active inputs must retain declared physical offsets, not be compacted after optimization (Task2).
2. Requested optimization must actually remain enabled; reflection must not invent identities (Task1).
3. A worker reporting the wrong layout must fail before producing a trusted snapshot (Task3).
4. Layout selection must affect compile identity even when a stage's code is identical (Task2/3).
5. Legacy requests and baseline shader hashes must remain stable (Task1/2/3).

## Task 1 — Preserve reflection through optimized compilation

**Files:** modify `tools/shader-probe/shader_probe.py`, `backend_protocol.py`, and only if needed the metadata handling in `spirv_parser.py`/`lower_xenos.py`; tests in `tests/shader_frontend_test.py`, `tests/shader_backend_protocol_test.py`; update `docs/x360-shader-host-protocol.md`.

**Interfaces:** keep `compile_hlsl_from_source(...)` and `compile_request(...)` request semantics. Optimization remains controlled by `options.optimize`; any effective internal metadata-preservation policy belongs to tool identity and is documented.

- [ ] Add failing real-compiler tests for the shared Textured PS with optimize=true, debug_info=false. Assert resource names BatchTexture/BatchSampler and actual reflection, not merely successful compilation.

```python
code, result, stderr = run_protocol(request(
    shared_source, 'pixel', entrypoint='PS', variant='Textured',
    options={'debug_info': False, 'optimize': True,
             'warnings_as_errors': False}))
self.assertEqual(code, 0, (result, stderr))
self.assertTrue(result['success'])
self.assertEqual({'BatchTexture', 'BatchSampler'},
                 {item['name'] for item in result['reflection']['bindings']})
```

- [ ] Run `python -m unittest discover -s tests -p 'shader_backend_protocol_test.py'`; record the missing-OpName RED, not an environment failure.
- [ ] Compare real optimized shaderc output with metadata/debug generation enabled. Implement explicit internal name preservation while retaining shaderc performance optimization. Keep requested debug output semantics distinct from internal reflection metadata. If this does not retain required names, stop this task with concrete evidence rather than inventing names or joining unrelated optimized/unoptimized IDs.
- [ ] Add tests for named matrix members, optimized includes, both debug_info values, and diagnostic behavior when required names genuinely cannot be established. No new runtime/debug instructions may slip past the lowerer's supported-opcode checks.
- [ ] Run focused tests and the existing Python suite. Assert the unoptimized baseline VS/PS binary SHA256 remains unchanged. Document effective policy and RED/GREEN evidence.

## Task 2 — Explicit physical vertex layouts in the Python protocol

**Files:** create `tools/shader-probe/vertex_layout.py`; modify `backend_protocol.py`, `backend_layout.py`, and `lower_xenos.py` only where necessary to consume validated layout data; create `tests/shader_vertex_layout_test.py`; update protocol documentation.

**Interfaces:** add optional request field `vertex_layout` with exactly `mesh3d-v1` or `batch2d-v1`. Missing field normalizes to `mesh3d-v1` for existing protocol1 clients. Wrong type/unknown profile fails. Include the normalized profile in compile identity. Emit selected layout identity and validated fetch metadata; do not select profiles from reflected names. `reflect_layout(binary, stage, entrypoint, vertex_layout='mesh3d-v1')` consumes the selected profile.

- [ ] Write RED tests: shared Textured VS under explicit batch profile must compile; the same source under default mesh profile must still reject float4 POSITION. Unknown profile must fail before shaderc invocation.

```python
batch_request = request(shared_source, 'vertex', entrypoint='VS',
                        variant='Textured', vertex_layout='batch2d-v1')
code, result, stderr = run_protocol(batch_request)
self.assertEqual(code, 0, (result, stderr))
self.assertEqual('batch2d-v1', result['metadata']['vertex_layout'])
for item in result['metadata']['inputs'].values():
    self.assertEqual(0, item['fetch_constant'])
    self.assertEqual(24, item['stride'])
    self.assertEqual('32_32_32_32_FLOAT', item['format'])
```

- [ ] Run the focused test and record the expected profile/layout rejection.
- [ ] Inspect the actual shared VS SPIR-V interface locations and source semantics. Bind the explicit profile's six fields to those stable locations and offsets; validate active input widths and reject unknown locations. Assert exact Textured active offsets0,4,8 only after confirming actual reflection. Do not renumber sparse inputs.
- [ ] Keep mesh profile rules exactly as before: LOCATION0 float3/off0, LOCATION1 float3/off3, LOCATION2 float2/off6, LOCATION3 float4/off8, stride12. Batch profile uses float4 fields and stride24. Both use vf0; texture uses tf1.
- [ ] Add controlled sparse-input tests retaining full stride, wrong-width/offset/profile tests, and profile-identity tests with identical PS source under both selections.
- [ ] Update tool-source identity enumeration to include the new module. Run full Python suite and ensure any diagnostic GPU-header hash remains unchanged.

## Task 3 — Carry and validate layout selection in the C# host

**Files:** create `builder/shaders/Xbox360ShaderVertexLayout.cs`; modify `Xbox360ShaderCompilerOptions.cs`, `Xbox360ShaderCompiler.cs`, `Xbox360ShaderCompileResult.cs`, `Xbox360ShaderProtocolMapper.cs`, `Xbox360ShaderProgram.cs`, `Xbox360ShaderProgramFactory.cs`; update `builder/helengine.x360.builder.csproj` only to copy the new Python module. Update affected test helpers/fixtures and create `builder.tests/Xbox360SpriteBatchShaderTests.cs`.

**Interfaces:** local enum `Xbox360ShaderVertexLayout { Mesh3D, Batch2D }`; append optional `vertexLayout = Xbox360ShaderVertexLayout.Mesh3D` to compiler options. Reject unknown enum values. Compiler serializes exact profile IDs, mapper validates returned profile against requested local configuration, typed result preserves selected profile, and vertex program snapshots expose it. No change to shared IShaderBackend/ShaderCompileRequest contracts.

```csharp
var options = new Xbox360ShaderCompilerOptions(
    shadercLibraryPath: shadercPath,
    vertexLayout: Xbox360ShaderVertexLayout.Batch2D);
var backend = new Xbox360ShaderBackend(options);
// Compile the real shared source using the shared catalog's Textured entrypoints
// and defines; FromCompilation validates each result, then Link validates the pair.
```

- [ ] Add RED integration tests consuming SpriteBatchShaderVariants.All and reading the existing shared source. Select Textured by exact catalog identity; do not embed a replacement shader. Report missing shaderc/source as a prerequisite error, not a skipped success.
- [ ] Run `dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --filter FullyQualifiedName~Xbox360SpriteBatchShaderTests`.
- [ ] Implement the local configuration/result path and profile-aware factory checks. Validate full stride/offset/type/fetch consistency against the selected profile. Keep pixel varying validation independent from vertex fetch layout. Preserve mesh-default callers and update test-only constructor cloning explicitly if the internal result signature changes.
- [ ] Extend protocol fixtures to echo the expected profile; test mismatched/unknown profile replies and immutable layout snapshots. Ensure new module is copied alongside the other bundled tools and included in tool identity.
- [ ] Verify real Textured pair with optimize=false and true: b0 projection64bytes/four VS constant registers, no PS constants, texture/sampler mapped to tf1, correct stage interfaces, and exact pair upload costs derived from independently observed word counts. Do not reuse the older fixture's 110-dword expected value.
- [ ] Verify arbitrary program labels do not change layout selection and the request preserves the selected catalog entrypoints/defines/variant. The generic backend must not become a hardcoded catalog dispatcher. Keep RoundedShape compilation rejection explicit; do not broaden lowering here.
- [ ] Run full local .NET and Python suites. Record all failures, including the previously intermittent process-timeout test. Do not weaken that test.

## Delivery and subsequent gates

- [ ] Task-scoped reviews with fixes/rechecks; no unrelated branch cleanup or commit.
- [ ] Update `docs/x360-shader-program-layout.md` and implementation progress with tested profiles, source hash, tool identity, commands/counts and remaining limits.
- [ ] Self-check that no shared shader/catalog/core or PS3 file was modified, no cache enabled and no material capability advertised.

This plan implements only the amendment's first host-side Textured slice.
RoundedShape lowering is a subsequent task requiring its actual opcode inventory
and numerical acceptance tests. Serializer/C-reader follow with the proven
explicit layout model; the shared Batch2D sink and Xenia validation remain later
gates in the original pipeline plan. No prerequisite approval for those later
implementation details is implied by completion of this slice.
