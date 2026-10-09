# Phase 3a: validated host shader programs and pair linking

Read this first. Existing checkout only, no worktrees/commit/subagents/emulator/UI.
You are not alone; preserve all unrelated edits and shared Engine files.
Use Luna implementation, TDD, apply_patch. This is the first bounded Phase3
slice: normalize the compiler result and validate a VS/PS pair BEFORE a binary
serializer/C reader or cooker relies on it. Do not activate material selection.

## Ownership and API

New files only, under builder/shaders:
- Xbox360ShaderProgram.cs: immutable typed snapshots used by a future artifact writer.
- Xbox360ShaderProgramFactory.cs: FromCompilation(Xbox360ShaderCompileResult).
- Xbox360ShaderProgramPair.cs: Link(vertex,pixel), program control and upload costs.
- Small dedicated metadata-reader helper if necessary; no giant all-purpose abstraction.
New builder.tests/Xbox360ShaderProgramTests.cs and Xbox360ShaderProgramPairTests.cs
(shared new test helper allowed). New docs/x360-shader-program-layout.md.
Do not edit Python, existing compiler/process code/tests, shared Engine, runtime,
csproj or renderer. Test corruption may clone the internal compile-result constructor
using a TEST-ONLY reflection helper; do not add production methods solely for tests.
Use contextual InvalidDataException/ArgumentException for malformed input.

Program snapshots preserve source path, program name, entrypoint, variant, stage,
tool/compile identities (64 hex chars), binary SHA256, microcode uint32 words decoded
from big-endian bytecode, register count/encoded field, typed inputs/outputs,
logical/physical resource mappings and constant layout. Collections must be truly
read-only wrappers over copies, not public writable arrays exposed as IReadOnlyList.
Do not retain caller-mutable ShaderProgramDefinition arrays or bytecode arrays.
No disk I/O, process launch, cache, binary file format, GPU upload or default constants.

## Validation from actual compiler contract

Accept only successful Xbox360 model4.0 vertex/pixel results. Request, binary and
program definition must agree on target/stage/entry/program/variant where those
fields exist. Require nonempty aligned microcode, dword count divisible by3 and
<=1024 (current Xenos instruction triples). Decode words explicitly big-endian.
Validate metadata stage/entrypoint, binary_dword_byte_order=big_endian,
unsupported_features empty, register_count1..64, highest_register_index=count-1,
sq_program_cntl_register_field=count-1 in0..63. Do not claim full ISA validation.
Bound JSON metadata size (8MiB UTF8); reject duplicate keys/type mismatches and
invalid required fields with context. Do not catch invalid input and return defaults.

Use reflection LOCATION/index and SV_POSITION/index0 plus native metadata;
never infer anything from source/program/resource names. Width float/float2/3/4.
Only VS output SV_POSITION float4; VS inputs have no builtin; PS has no builtin
inputs and exactly one float4 LOCATION0 output. Reject duplicate interfaces.
VS inputs actually present only:0=float3 offset0,1=float3 offset3,2=float2 offset6,
3=float4 offset8; vf0 stride12. Inputs can be a subset. Native inputs/input_map
must agree with reflected locations, each other, register bounds, fetch/stride/
offset and hardware formats32_32_32_FLOAT/32_32_FLOAT/32_32_32_32_FLOAT.
PS inputs are LOCATION0..7 with metadata kind=interpolator and register=location.
Native varying_map: VS user outputs / PS inputs exactly, slot=location in0..7.
Native outputs must match Position/varying/color reflection; no hidden extras.
Debug source strings/temporary purposes need not become part of runtime layout.

Resources: zero or one b0/set0 per stage; logical binding slot equals request
ConstantBufferShift (physical binding0). Size positive, padded16, <=4096bytes.
Declared members have unique names, float scalar/vector or float4x4, exact sizes
4/8/12/16/64, vec4-aligned offsets, nonoverlap and checked within buffer.
Native uniforms only key"0", binding0/set0, raw_memory_order exactly
consecutive_uploaded_vec4_registers, symbolic register_count=size/16; reject
register_values on this production route. Native members are USED accesses, not
necessarily all declared members: match offset to declared member, exact contiguous
registers from offset/16, 1 register for scalar/vector or4 for matrix. Matrix
requires major row_major/column_major and stride16; scalar/vector has no matrix
decorations. Keep declared members separately from used member/matrix layout.
Do not invent orientation for an unused matrix. Reject duplicate used accesses
or access outside the declared member. Preserve bank base0 VS /256 PS.
If reflection declares a buffer but native uniforms omit it entirely, reject as
unsupported/inconsistent until an actual compiler case establishes a safe rule;
do not silently drop the reflected resource. Zero buffer means zero uniform capacity.

Textures: zero or one Texture2D and one Sampler, both must be present together;
pixel only, set0, no members/size. Native textures exactly one matching pair if
present, stage=pixel, dimension=2D, physical tf1. Derive raw binding by subtracting
request texture/sampler shifts with checked arithmetic; reject negative, mismatch
or overflow. Names are carried, never used to choose pairing. Vertex vf0 does
not alias tf1 (texture slot occupies vertex fetch indices3*tf..3*tf+2).
Reject unknown/multiple resource types and missing native mappings. Uniforms,
textures, register fields, interfaces and logical slots are separate concepts.

## Pair linking and cost

Require vertex then pixel, same variant and same tool identity; program names
and entrypoints may differ. Every PS input needs a VS output at the same location
and width AND physical varying slot; extra VS outputs are valid.
ProgramControl = vs.RegisterField | (ps.RegisterField<<8) |
                 (max VS user-output slot, or0 when none, <<20).
Keep actual interpolator count (highest slot+1 or0) separately from encodedfield.
No magic baseline names or exact diagnostic-only position/color/UV requirement.

ProgramUploadDwords = vs.WordCount + ps.WordCount +6 (3 per program packet).
ConstantUploadDwords = sum across stages: capacity*4 + (capacity>0 ?2:0).
StateUploadDwords=10 for five two-dword SQ register writes used by current runtime.
PairUploadDwords = sum of the three. Cost covers ONLY pair/constant/SQ uploads,
not vertex/texture fetch setup, draw state, other draws or whole frame safety.
Expose a ValidateUploadBudget(availableDwords) check: positive integer, accepts
exact equality, rejects insufficient, cannot accept >8192 safe runtime capacity.
Do not pretend to reserve the ring or change its limit; caller must supply actual
available budget later. All arithmetic checked.

## Acceptance

Write RED before implementation. Use REAL compiler VS transform / PS texture
fixtures via bundled Python and installed shaderc (existing integration tests
show setup). Baseline51VS/21PS words, VS9registers field8, PS inspect actual,
VS5constant registers, PS0; ProgramUpload78 +Constant22 +State10 =110dwords.
Assert source microcode hashes unchanged. Compare exact hand-derived fields,
not expected values produced by the code under test.
Real extra VS varyings allowed and missing/mismatched PS varyings rejected.
Color-only pair with no textures valid. Nondefault logical shifts must not alter
tf1/vf0/constant physical banks. Parameter values are not compiled into this model.
Corrupt JSON/register fields/stage/entry/binary order/word length/type, mismatched
physical maps, duplicate/out-of-range interfaces, invalid cb member/major/stride,
texture pair mismatch, unknown resources, and identity mismatch fail explicitly.
Cover register64/field63, binding indices>255 and constant offset>255 using
well-formed controlled metadata (binary need not execute in these validation tests).
Test immutable snapshots against original arrays changed after construction;
pair budget equality/one-less/>8192. No renderer state side effects.

Run targeted new tests then full local suite with
dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore
Report every failure: existing timeout test had one cold-start failure previously
and then passed isolated/full32; do not weaken or silently hide it or edit it here.
No need to rerun Python suite if Python unchanged. Report
docs/superpowers/plans/x360-shader-program-pair-report.md (RED/GREEN, exact commands,
counts, files, limitations), return concise completion or concrete blocker.

## Parent evidence and boundaries

Verify-tier graph C-dev-helworks-helengine-x360 generation2026-09-24T00:00:54Z,
queried Xbox360ShaderCompileResult/lower_spirv/shader probe; relevant paths all
metadata_changed, shader_gpu_probe.inc parse_unusable. Parent read actual result,
lowerer metadata/input/used-uniform/texture generation, gpu_probe validation/cost,
and runtime probe upload. Existing gpu_probe validator is DIAGNOSTIC-ONLY and
requires exact texture/vertex fixture: do not reuse its fixture restrictions.
Shared originalXbox descriptor now present; latest fullsuite32/32 on rerun.
No shared edits required. User-authorized plan is Phase3; this task establishes
typed input and pair validation, not a serialized guest ABI or material activation.
