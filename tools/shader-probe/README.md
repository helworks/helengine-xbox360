# Bounded Xenos shader probe

`xenos_probe.py` is a CPU-only experiment that compiles a deliberately small
SPIR-V subset into provisional Xenos assembly and big-endian dword bytes. It is
not connected to the runtime shader loader and its artifacts are not a final
runtime format.

The supported slice is straight-line float32 vertex transform/color/UV export
and pixel uniform, interpolated-color, and bounded texture arithmetic. Pixel
sampling supports `Texture2D<float4>` with a separate `SamplerState`, normalized
float2 coordinates, and plain implicit-LOD sampling, followed by the supported
float arithmetic. Texture and sampler handles are paired through the explicit
`texture_bindings` layout; descriptor set/binding numbers are logical IDs and
are never treated as physical Xenos slots. Sampling filter and address state
remains in the runtime fetch constant. Vertex transforms are limited to the
explicitly decorated matrix/storage-operation combinations in the lowerer.
Control flow, division, unsupported resources or sampling operands, additional
descriptor forms, unsupported decorations/capabilities, and other SPIR-V
operations fail explicitly. Register allocation is conservative and does not
reuse temporary registers by liveness.

## Run

For HLSL, provide the installed `shaderc_shared.dll` either with `--shaderc` or
`SHADERC_SHARED_LIBRARY`:

```powershell
python tools/shader-probe/xenos_probe.py `
  --input tests/shaders/fixtures/transform_vs.hlsl `
  --stage vertex --entry main `
  --layout tests/shaders/fixtures/xenos_runtime_layout.json `
  --output-prefix out/transform `
  --shaderc C:/path/to/shaderc_shared.dll
```

The input may instead be precompiled `.spv`; that route does not need shaderc:

```powershell
python tools/shader-probe/xenos_probe.py `
  --input out/transform.spv --stage vertex --entry main `
  --layout tests/shaders/fixtures/xenos_runtime_layout.json `
  --output-prefix out/transform-spv
```

Both routes require an explicit JSON layout and write `.xasm`, `.bin`, and
`.json` alongside the supplied output prefix. Existing explicit output files
are invalidated before work starts and removed if compilation/lowering fails;
source, layout, shaderc library, and tool paths are protected from output-path
aliasing. The metadata includes input/source/SPIR-V/layout hashes and the
shaderc DLL hash for HLSL input.

## Layout contract

`tests/shaders/fixtures/xenos_runtime_layout.json` is the concrete vertex
layout used by the numeric test: all attributes refer to `vf0`, the physical
vertex has a 12-dword stride, position starts at dword 0 (`float3`), UV at
dword 6 (`float2`), and color at dword 8 (`float4`). The `offset` and `stride`
fields use dwords, matching the assembler's `vfetch_full` convention, not
bytes. Uniform entries are explicit uploaded float4 register values in
register order; matrix values are raw consecutive memory chunks as described
by the member's SPIR-V `RowMajor`/`ColMajor` and `MatrixStride` decorations.
Varying locations map explicitly to bounded Xenos interpolator slots.

For the concrete texture pairing, see
`tests/shaders/fixtures/texture_runtime_layout.json`. It maps logical texture
set 0/binding 0 plus sampler set 0/binding 0 to physical `tf1`, matching the
current runtime texture upload path. The compiler accepts physical slots
0..31, but a future linked program must validate collisions between vertex and
texture fetch allocations before runtime use; a valid texture-pair layout by
itself does not prove linked allocation safety.

The transform fixture's nonsymmetric matrix is checked by executing decoded
emitted microcode against one physical vertex array; the expected position is
`[30, 107, 211, 321]`. Pixel tests execute decoded arithmetic and texture-fetch
instructions, including a changed UV expression and sample-dependent color
arithmetic. The texture CPU executor uses a deliberately small point/clamp/no-
mip model at safe interior UV points chosen to route to distinct texels. It
checks algebraic and channel routing; it does not model the current runtime's
linear min/mag filtering, derivatives, interpolation, or device behavior. The
test executor is intentionally narrower than the assembler and rejects
instructions it does not understand. It rounds normal finite values and each ALU result to float32;
MAD rounds the product before the sum, following the current Xenia translator
reference. Denormals, NaNs, and hardware rounding details are not modeled.

Run focused lowering/CLI tests with:

```powershell
python tests/shader_lowering_test.py -v
```

Then run the repository Python suite with:

```powershell
python -m unittest discover -s tests -p '*_test.py' -v
```

Passing CPU tests do not establish runtime integration, shader loading,
interpolation/filtering behavior on hardware, or Xenia/GPU correctness. Gate A
remains open pending isolated GPU execution; CPU evidence alone must not enable
the shared shader backend.

## In-memory compiler request API

`shader_probe.compile_hlsl_from_source(library, source_bytes, source_name,
stage, entrypoint, defines=None, include_resolver=None, *, debug_info=False,
optimize=False, warnings_as_errors=False)` compiles caller-owned HLSL bytes
with shaderc's HLSL preprocessor. It reads no source or include files. The
resolver receives `(requesting_source_name, requested_include_name)` and must
return `(resolved_source_name, source_bytes)`; returning `None` reports a
missing include. Shaderc only calls it for active includes, and nested requests
use the resolved filename as their requesting source.

The result is `ShaderCompileResult(spirv, diagnostics, included_sources)`.
`spirv` is `None` on shaderc compile failure, while `diagnostics` is retained
for both success and failure. Each `included_sources` item is an
`IncludedSource(source_name, sha256)` for the exact bytes supplied by the
resolver. Resolver exceptions and malformed results become compiler
diagnostics. Include nesting is limited to 64 levels. Source, filename,
entrypoint, define names/values, and returned include payloads reject embedded
NULs; define names must be C-style identifiers.

`debug_info`, `optimize`, and `warnings_as_errors` map to shaderc compile
options. The default values preserve the existing `compile_hlsl` output.
These options affect the frontend SPIR-V only; downstream Xenos lowering may
reject modules outside its supported subset. The path-based
`compile_hlsl(library, source_path, stage, entrypoint)` remains a SPIR-V-bytes
API and reports shaderc failures as `ValueError`.

## Symbolic uniform capacity

The lowerer also accepts a stage-local uniform capacity without upload values,
for example `"uniforms": {"0": {"register_count": 5}}`. This declares
float4 constants c0 through c4; it does not create or initialize those values.
The lowerer emits the same constant-register references as a value-based
layout and rejects a shader that references a register beyond the declared
capacity. Counts must be integers from 1 through 256, and cannot be combined
with `registers` or `values` in the same binding entry.

Symbolic uniform metadata reports `register_count` and the usual descriptor,
binding, memory-order, and member layout, without `register_values`. The
standalone GPU probe still needs real upload values to build its upload data;
a symbolic layout is only sufficient for lowering and cannot be used to
synthesize those values.
