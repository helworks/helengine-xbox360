# Phase 1 bounded 2D texture lowering report

## Result

Implemented a CPU-compiler slice for pixel-stage `Texture2D<float4>` with a
separate `SamplerState`, normalized `float2` coordinates, plain
`OpImageSampleImplicitLod`, and the lowerer's existing float arithmetic. The
real `texture_ps.hlsl` fixture compiles through the installed shaderc DLL,
lowers to `tfetch`, and its emitted microcode passes the small CPU routing
oracle.

Texture/sampler resources are paired using explicit `texture_bindings` entries
with `texture_set`, `texture_binding`, `sampler_set`, `sampler_binding`, and
`fetch_constant`. Mapping fields require exact integers; logical pair
duplicates, out-of-range values, and physical-slot collisions are rejected.
No slot-zero fallback exists. Metadata records each sampled logical pair,
physical fetch slot, pixel stage, and `2D` dimension. Sampling filter and
address behavior remains in the runtime fetch constant.

## TDD evidence

RED command:

```text
python tests/shader_lowering_test.py LoweringTests.test_texture2d_sample_requires_and_records_explicit_logical_pair_mapping -v
```

Before implementation, the new test failed at the expected existing guard:
`unsupported SPIR-V resource/storage class UniformConstant`.

GREEN focused command:

```text
python tests/shader_lowering_test.py -v
```

Result: `Ran 25 tests ... OK`.

GREEN required full-suite command:

```text
python -m unittest discover -s tests -p '*_test.py'
```

Result: `Ran 51 tests ... OK`.

The texture test decodes the emitted fetch source/destination, swizzle,
dimension, nonzero physical slot, normalized-coordinate flag, computed-LOD
flag, and zero baked-in LOD bias. It verifies serialized CF dependency bits
and ALU-coordinate/fetch/dependent-ALU order. A changed authored UV expression
selects a different texel; asymmetric UVs and distinct RGBA values check U/V,
RGB, and alpha routing.
The CLI success case uses `tf1`; a missing-map failure removes stale outputs.

## Accepted subset and limits

Accepted resources are sampled float32 2D images with `Depth=0`,
`Arrayed=0`, `MS=0`, `Sampled=1`, plus a separate sampler. The operation must
be `OpSampledImage` followed by `OpImageSampleImplicitLod` with exactly its
four required operands. Texture coordinates must be float2 values held in one
temporary register, and sample results must be float4. The existing
conservative register allocation is retained. The local `tfetch` assembler is
unchanged; its encoding uses normalized coordinates, fetch-constant filter
settings, 2D dimension, and computed LOD. Each generated instruction is
serialized to retain dependency order.

Non-pixel sampling, non-2D images, depth/comparison and integer images,
storage/unknown image forms, descriptor arrays, texture arrays, multisample
images, biased/offset/other optional sample operands, explicit-LOD and other
image operations, malformed operand lengths, unmapped pairs, and invalid or
colliding mappings fail explicitly.

The deterministic test sampler is point/clamp/no-mip and uses safe interior UV
points that select different texels. It is only an algebra and routing oracle; it does not match the
runtime's linear min/mag filtering or validate derivatives, interpolation,
hardware behavior, or Xenia rendering. The runtime example maps to `tf1`.
Texture slot `tf0` overlaps vertex fetch constants `vf0..vf2`; linked
vertex/texture allocation collision validation remains required before runtime
use. Gate A remains open pending isolated GPU execution; CPU evidence alone
does not enable the shared shader backend.

## Changed files

- `tools/shader-probe/lower_xenos.py`
- `tools/shader-probe/spirv_parser.py`
- `tools/shader-probe/README.md`
- `tests/shader_lowering_test.py`
- `tests/shaders/fixtures/texture_ps.hlsl`
- `tests/shaders/fixtures/texture_runtime_layout.json`
- `docs/superpowers/plans/x360-shader-texture-report.md`

No assembler, runtime, frontend, builder, shared Engine, or PS3 files were
changed.

## Scoped review fix: asymmetric UV evidence

The review found that the original fixture used equal U and V coordinates, so
swapping the emitted texture source lanes could still select the same texel.
The test now uses input UV `(0.6, 0.8)`: the authored square expression produces
`(0.36, 0.64)`, safely inside texel `(x=0, y=1)`. A changed direct-UV expression
uses `(0.6, 0.8)`, safely inside texel `(x=1, y=1)`. Swapping X/Y selects
`(x=1, y=0)`. These points remain away from the 0.5 boundaries, and each
selected texel has distinct RGBA values.

The test applies the X/Y source-swizzle swap to a temporary in-memory copy of
the emitted binary and confirms that its RGBA result fails the unmutated
expected-output assertion. No mutated binary or production-source change is
left behind. The updated focused test passed:

```text
python tests/shader_lowering_test.py LoweringTests.test_texture2d_sample_requires_and_records_explicit_logical_pair_mapping -v
Ran 1 test ... OK
```
