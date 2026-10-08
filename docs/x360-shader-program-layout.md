# Xbox 360 host shader program layout

Phase 3a adds a validated, immutable host-side representation for one compiled
Xenos shader stage and a linker for a vertex/pixel pair. It is an in-memory
boundary for a future artifact writer; it is not a wire format or runtime
activation path.

## Constructing a program snapshot

Call `Xbox360ShaderProgramFactory.FromCompilation(Xbox360ShaderCompileResult)`.
The factory accepts successful Xbox 360 shader-model 4.0 vertex or pixel
results whose request, program definition, binary, and compiler metadata agree
on the stage and identity fields they expose. It preserves
`Request.Source.Path`, program name, entrypoint, variant, tool identity, and
compile identity. It also preserves the locally selected `VertexLayout` profile;
the protocol mapper requires the worker's `metadata.vertex_layout` to name the
same profile as the request. Source path remains distinct from any future
artifact asset ID; this phase does not synthesize IDs.

The snapshot contains:

- big-endian-decoded `uint` microcode words and their bytecode SHA-256;
- the metadata register count and encoded register field;
- typed input/output interfaces and validated physical resource mappings;
- the selected immutable `Mesh3D` or `Batch2D` vertex-fetch profile;
- the declared constant-buffer members separately from the members actually
  accessed by the compiled shader.

All exposed collections are read-only wrappers around copied data. The
snapshot does not retain caller-mutable definition or bytecode arrays.

## Validation boundary

The metadata reader accepts at most 8 MiB of UTF-8 JSON, requires an object
root, rejects duplicate properties and wrong JSON types, and does not substitute
defaults for missing required values. Program metadata must report the matching
stage and entrypoint, `binary_dword_byte_order: "big_endian"`, no unsupported
features, and a register count in 1..64. The highest register index and encoded
program register field must each equal `register_count - 1`. Microcode must be
nonempty, dword-aligned, divisible into instruction triples, and no more than
1024 words. This is metadata/contract validation, not full ISA validation.

Reflection and native metadata are checked together. Active vertex inputs are
validated against the selected fixed `vf0` profile. Inputs omitted by the
compiler because they are unused need no native input record; every reported
input must match the profile exactly.

| Profile | Input location | Format | Offset (floats) | Stride (floats) |
| --- | ---: | --- | ---: | ---: |
| `Mesh3D` | 0 | `float3` | 0 | 12 |
| `Mesh3D` | 1 | `float3` | 3 | 12 |
| `Mesh3D` | 2 | `float2` | 6 | 12 |
| `Mesh3D` | 3 | `float4` | 8 | 12 |
| `Batch2D` | 0 | `float4` | 0 | 24 |
| `Batch2D` | 1 | `float4` | 4 | 24 |
| `Batch2D` | 2 | `float4` | 8 | 24 |
| `Batch2D` | 3 | `float4` | 12 | 24 |
| `Batch2D` | 4 | `float4` | 16 | 24 |
| `Batch2D` | 5 | `float4` | 20 | 24 |

`Mesh3D` is the default for generic backend callers. The selected profile is
sent as `mesh3d-v1` or `batch2d-v1` and participates in compile identity,
including pixel stages whose binary may be identical for both profiles. Pixel
varying validation uses shader interfaces and does not apply vertex fetch
widths.

The per-location native values for the `Mesh3D` profile are:

| Input location | Format | Offset | Stride |
| --- | --- | ---: | ---: |
| 0 | `float3` | 0 | 12 |
| 1 | `float3` | 3 | 12 |
| 2 | `float2` | 6 | 12 |
| 3 | `float4` | 8 | 12 |

Vertex user varyings use locations 0..7 with the native slot equal to location;
the vertex position output is `SV_POSITION0 float4`. Pixel inputs are location
varyings with matching native slots, and the pixel output is exactly
`LOCATION0 float4` in the host reflection layout. HLSL fixtures spell the
source semantic `SV_Target0`; the protocol mapper represents numeric SPIR-V
locations as `LOCATION`, which is the value consumed by this snapshot.
Duplicate interfaces/exports, hidden native exports, and
inconsistent reflection/native maps are rejected. Pair linking permits unused
extra vertex varying outputs; it does not impose diagnostic-fixture-specific
position/color/UV requirements beyond each stage's interface contract.

Supported resources are limited to zero or one constant buffer at logical
`b0/set0` per stage and, for pixel programs only, an optional `Texture2D` plus
sampler pair. Constant-buffer logical slot follows the request's
`ConstantBufferShift`; its physical binding is 0. The constant buffer must be
positive-sized, 16-byte padded, and no larger than 4096 bytes. Member names are
unique; supported types are float scalar/vectors and `float4x4`, with exact
sizes, aligned nonoverlapping offsets, and bounds checked against the buffer.
The physical constant bank base is 0 for vertex and 256 for pixel programs.

Native uniform records describe used members, not necessarily every declared
member. Each used member must match one declared member at the exact offset and
have the exact contiguous uploaded vec4-register range. Matrices additionally
require `row_major` or `column_major` and stride 16; unused matrices have no
inferred orientation. Missing native uniform metadata for a declared buffer,
duplicate accesses, partial/nested accesses, and out-of-member accesses are
rejected rather than guessed.

Textures and samplers must be present together, have no members or size, and
map to pixel `tf1`. Their raw logical binding is derived by checked subtraction
of the request's respective binding shift. Names are retained as data and are
not used to pair resources. Vertex `vf0` and pixel `tf1` mappings remain
independent of those logical shifts.

## Pair linking and upload cost

Link a pair with `Xbox360ShaderProgramPair.Link(vertex, pixel)`. It requires
vertex then pixel stages, Xbox 360 targets, the same variant, and the same tool
identity; program names and entrypoints may differ. Every pixel input must match
a vertex output on location, width, and physical varying slot. Extra vertex
outputs are allowed.

The pair exposes these derived values:

```text
highestVaryingSlot = max(vertex user-output physical slot), or 0 when none
InterpolatorCount = highestVaryingSlot + 1, or 0 when none
ProgramControl = vertex.RegisterField
               | (pixel.RegisterField << 8)
               | (highestVaryingSlot << 20)
ProgramUploadDwords = vertex.WordCount + pixel.WordCount + 6
ConstantUploadDwords = sum(capacity * 4 + (capacity > 0 ? 2 : 0))
StateUploadDwords = 10
PairUploadDwords = program + constant + state
```

`ValidateUploadBudget` accepts only 1..8192 available dwords, accepts exact
equality with the pair cost, and rejects insufficient capacity. The cost covers
only program, constant, and the five two-dword SQ register writes; it excludes
vertex/texture fetch setup, draw state, other draws, and whole-frame safety.

The real transform-vertex/textured-pixel Mesh3D baseline yields
`ProgramControl = 0x00100408`, two interpolators, and 110 dwords total
(78 program + 22 constants + 10 SQ state). No GPU upload, default constants,
binary serialization, guest reader, cooker, renderer changes, or material
selection are part of this phase.

The shared `SpriteBatchShaderVariants.Textured` Batch2D pair compiles to 45
vertex dwords and 15 pixel dwords in both optimization modes. Its projection
buffer is 64 bytes/four vec4 registers, the pixel stage has no constant buffer,
and the texture/sampler pair maps to `tf1`. Varying slots are 0 and 1. The pair
cost is `ProgramControl = 0x00100304`, 66 program-upload dwords, 18 constant
upload dwords, 10 state dwords, and 94 dwords total. This is host-side compile
and metadata evidence only; it does not prove GPU upload or rendered output.
