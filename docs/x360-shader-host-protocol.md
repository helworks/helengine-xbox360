# Xbox 360 shader host protocol

The reusable host compiler runs one request per process:

```text
python -u <shader-probe/backend_protocol.py> --shaderc <absolute shaderc_shared.dll path>
```

In the source checkout the script is `tools/shader-probe/backend_protocol.py`.
Builder deployments place it at `shader-tools/shader-probe/backend_protocol.py`
and place `xenos_asm.py` in the parent `shader-tools` directory. The script
resolves that sibling layout from its own location. Python writes no progress or
diagnostic text to stdout; its only stdout messages are protocol JSON lines.

Each UTF-8 JSON line is limited to 8 MiB. The first line is the frozen protocol
1 `compile` message described in
[`x360-shader-host-protocol-brief.md`](superpowers/plans/x360-shader-host-protocol-brief.md).
It requires the complete field set, exact `x360`/`4.0` target, vertex or pixel
stage, boolean option flags, and integer binding shifts. `default_space` must be
zero. Defines are passed unchanged to the in-memory HLSL frontend, which
validates identifiers and NULs. The host performs no file-based source or
include lookup. The optional string field `vertex_layout` accepts exactly
`mesh3d-v1` or `batch2d-v1`; omission is normalized to `mesh3d-v1`, while null,
non-string, and unknown values fail request validation before shaderc loads.
The normalized ID participates in compile identity for vertex and pixel stages.

For an active shaderc include, the host flushes an `include` message with a
monotonically increasing ID, then waits for an `include_result` with the same
protocol and ID. The caller supplies the resolved source name and contents, or
an error. Nested includes repeat this exchange; inactive preprocessor branches
do not generate requests. The host accepts at most 4096 requests and the
frontend enforces nesting depth 64. A missing or malformed response fails the
compile and produces no binary.

The host compiles the supplied HLSL bytes through `shader_probe`, reflects the
resulting SPIR-V through `backend_layout`, derives the bounded platform layout,
and passes that layout to `lower_xenos`. A successful result's metadata reports
the normalized `vertex_layout` for both stages. Uniforms use symbolic register
capacity; no constant values are fabricated. Resource set/binding values and
texture/sampler pairs remain the native reflected values. The requested
binding-policy shifts are retained in request identity; the managed consumer
uses them to calculate shared logical slots. Texture/sampler pairs map to
physical `tf1`; the selected vertex profile maps inputs to `vf0`.

`mesh3d-v1` preserves the original 12-dword stride mapping: location 0 is
`float3` at offset 0, location 1 is `float3` at offset 3, location 2 is
`float2` at offset 6, and location 3 is `float4` at offset 8. `batch2d-v1`
uses a 24-dword stride and six `float4` fields at locations 0..5 with offsets
0, 4, 8, 12, 16, and 20. Active sparse inputs keep their declared profile
offsets and full profile stride. Selection is explicit and does not depend on
shader names or the reflected subset. Both profiles use `vf0`; pixel textures
remain disjoint on `tf1`.

Success returns the frozen `result` message with base64 microcode, existing
lowerer metadata, actual SPIR-V interface/resource reflection, include content
hashes, and identities. Resource and member names come from SPIR-V `OpName` and
`OpMemberName` data. Unsupported or absent names needed for resource reflection
fail explicitly. Failure returns only `protocol`, `type`, `success:false`, and
diagnostics; the process exits nonzero and does not reuse output from another
invocation.

The host enables shaderc debug metadata internally when `optimize` is true so
reflection names survive performance optimization. The caller's `debug_info`
flag remains independent and both request flags remain unchanged in the compile
identity; `optimize:true` still selects shaderc's performance optimization
level. The intermediate SPIR-V is not returned. During Xenos lowering only
`OpLine` and `OpNoLine` source-location directives are skipped; unsupported
runtime instructions continue to fail explicitly.

`tool_identity` covers protocol/layout ABI version 1, both physical vertex
profile schemas, shaderc DLL bytes, and the protocol, layout, vertex profile,
frontend, lowerer, parser, and assembler Python sources.
`compile_identity` covers the canonical full request, resolved include
identities, tool identity, emitted binary, lowerer metadata, and reflection.
There is no host cache. A successful stage is limited to 1024 nonempty,
aligned microcode dwords. This per-stage bound does not prove that an arbitrary
vertex/pixel pair fits the runtime ring.

Focused real-DLL tests run with:

```powershell
python tests/shader_backend_protocol_test.py -v
```
