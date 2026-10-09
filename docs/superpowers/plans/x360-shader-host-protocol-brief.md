# Phase 2d: reusable compiler protocol and reflected platform layout

Read this first. Existing checkout only; no worktrees/commit/subagents/emulator/UI.
You are not alone: preserve other changes. TDD and apply_patch. Own NEW
`tools/shader-probe/backend_protocol.py`, `backend_layout.py`,
`tests/shader_backend_protocol_test.py`, `docs/x360-shader-host-protocol.md`.
Do not edit existing frontend/lowerer/assembler/parser/GPU generator or C#.

Reuse shader_probe.compile_hlsl_from_source and lower_xenos.lower_spirv. Phase2c
adds symbolic uniform mapping {"0":{"register_count":N}}, already agreed.
No second compiler, preprocessing by regex, fixture substitution or dummy constants.

## Frozen wire contract (C# consumes this)

Executable `python -u backend_protocol.py --shaderc ABSOLUTE_DLL_PATH`.
One compile per process, UTF-8 line-delimited JSON on stdin/stdout, flushed.
Stdout only protocol; diagnostics in final JSON (unexpected traceback may stderr).
Limit each line to 8MiB UTF-8, reject malformed/oversized/unsupported protocol.

Initial message:
`{"protocol":1,"type":"compile","source":"...","source_name":"...","stage":"vertex|pixel","entrypoint":"...","program":"...","variant":"...","target":"x360","shader_model":"4.0","defines":{"NAME":"value"},"options":{"debug_info":false,"optimize":false,"warnings_as_errors":false},"binding_policy":{"default_space":0,"constant_buffer_shift":0,"texture_shift":32,"sampler_shift":64,"storage_shift":96}}`.
Require the documented fields/types; unknown/malformed stage/target/model/flags
and nonzero default_space fail explicitly. Native binding indices stay raw;
shifts describe logical shared metadata only. Reject invalid defines/NUL via the
frontend. Preserve arbitrary nonempty program/variant/entry/name strings.

When shaderc requests an active include, send:
`{"protocol":1,"type":"include","id":1,"requesting_source":"...","include_name":"..."}`.
Read exactly matching response (max4096 include requests; frontend depth64):
success `{"protocol":1,"type":"include_result","id":1,"source_name":"resolved/path","source":"..."}`;
failure `{"protocol":1,"type":"include_result","id":1,"error":"reason"}`.
No implicit disk include lookup. ID/protocol/type mismatch or EOF fail usefully.

Final success:
`{"protocol":1,"type":"result","success":true,"binary_base64":"...","metadata":{...existing_lowerer_metadata...},"reflection":{"inputs":[...],"outputs":[...],"bindings":[...]},"diagnostics":"...","includes":[{"source_name":"...","sha256":"..."}],"tool_identity":"SHA256","compile_identity":"SHA256"}`.
Final failure: same protocol/type, success:false, diagnostics:string; no binary.
Exit0 for success, nonzero for failure. Never serialize stale prior output.

Reflection interface item: `{name:string,location:int|null,builtin:int|null,format:string}`
where format float/float2/float3/float4 and builtins are actual SPIR-V IDs.
Resource item: `{name:string,kind:"constant_buffer|texture2d|sampler",set:int,binding:int,size:int,members:[{name:string,type:string,offset:int,size:int}]}`.
Use real OpName/OpMemberName and decoration/type data from compiled SPIR-V.
Read member names from module.instructions if parser has no dedicated index.
Missing necessary resource names/types are an explicit unsupported-reflection
diagnostic, never invented source identifiers or regex-parsed original HLSL.
Metadata retains matrix orientation/stride and actual physical resource mapping.

tool_identity hashes protocol/layout ABI version1, actual shaderc DLL bytes and
functional Python files backend_protocol/backend_layout/shader_probe/lower_xenos/
spirv_parser/xenos_asm. compile_identity hashes canonical complete request,
effective include identities, tool_identity and emitted binary/metadata/reflection.
No cache here; changing source/include/options/variant/tool bytes must affect
identity even where output instructions happen to remain identical.

## Bounded platform ABI, derived from actual compiled interfaces

Use parser types/decorations; never select layout by asset/program name.
- float32 scalar/vector interfaces only; no arrays/struct interfaces/int/builtin
  inputs. Duplicate or out-of-range locations fail.
- VS input locations:0=float3 POSITION offset0,1=float3 NORMAL offset3,
  2=float2 UV offset6,3=float4 COLOR offset8, all vf0/stride12 dwords.
  Only map present inputs, require exact matching shape. No automatic semantic
  inference: this is a published location ABI; names do not choose bindings.
- VS must output BuiltIn Position0 float4; user outputs locations0..7 map to
  same Xenos varying slot. PS inputs locations0..7 same mapping; no builtin input.
- PS single float4 output at location0; no other outputs/builtins.
- At most b0/set0 uniform buffer; members float32 scalar/vector or float4x4,
  offsets vec4-aligned, matrix stride16 and exactly one major decoration. Reflect
  declared members and padded buffer size, derive register_count=ceil(size/16),
  max256. Reject unsupported member forms rather than fill placeholder values.
- At most one Texture2D<float4> and one separate sampler, both set0, PS only;
  if present require both and map their actual binding pair to tf1 (vf0 is disjoint).
  Reject additional resources/storage/arrays/unsupported images. Lowerer remains
  authoritative on operation restrictions. No pairing by source resource name.
- Reject empty/unaligned or >1024-dword single-stage microcode; this is a compiler
  bound, NOT proof that arbitrary VS/PS pair fits runtime ring. Pair linker comes later.

## Acceptance

Real shaderc protocol round trips for transform VS and textured PS; resulting
microcode matches existing validated hashes40C010... andD70B3A... without supplying
uniform values. Verify reflection names/member offsets/major metadata and logical
bindings from actual compiled include contents, independent of source filename.
Nested/conditional includes via live protocol; inactive include never requested;
missing include, malformed protocol, wrongID, missingtool, unsupported shader,
wrong interface shape fail with no binary. Source expressions change binary;
source/include/options/variant identity changes; no stale result on subsequent
new invocation. Test input/resource limits. Full existing suite stays passing.
Tests can call pure helper for layout validation plus subprocess integration.
Do not expand lowerer capabilities to make new shaders pass.

## Evidence and references

Parent Verify graph in C-dev-helworks-helengine-x360 generation2026-09-23T22:34:34Z;
coverage metadata_changed for lowerer/gpu_probe/tests/assembler; direct reads
confirm lowerer supports b0 only, pixel varying slots0..7, explicit texture pair
maps. gpu_probe._read_interface already shows parser-based type/location checks
but is diagnostic-only; keep production layout independent of diagnostic fixture
restrictions. spirv_parser.SpirvModule has types, variables, names, decorations,
member_decorations and instructions; OpMemberName remains in instructions.
Existing physical vertex stride12 includes normal at offset3. DLL path for tests:
C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll.

Report docs/superpowers/plans/x360-shader-host-protocol-report.md with RED/GREEN,
API details, test counts, paths and concerns. Return concise status.
