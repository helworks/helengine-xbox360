# Phase 2 integration boundary, 2026-09-23

This is a source-verified coordination note, not an implemented backend.

## Shared changes requiring coordination

Current shared `ShaderCompileTarget` ends with `WiiU=6`, `Ps3=7`. Both target
files are clean in the shared worktree, unlike earlier uncommitted PS3 edits.
The proposed Xbox addition is append-only `Xbox360=8`, descriptor `x360` /
`HEL_API_X360`. Do not introduce numeric casts as a temporary target or alias
Xbox compilation to another platform. Confirm ownership before the patch.

Coordination update: user answered "eu acho que ja foi?". Parent verified the
target change is committed in shared Engine as `b806163d` (PS3 target/provenance),
and the three existing shared files remain clean. Proceed with the minimal
append-only Xbox identity and optional host cache policy; recheck before writing.

`ShaderCompileService.Compile` gets the backend, computes a request cache key,
looks up the cache, and only then calls the backend. An internal Xbox identity
cannot correct a shared stale hit. The current key includes source text,
program, entry, stage, target, shader model, variant, defines and binding policy;
it omits include contents, tool identity and three boolean compile flags.

Implemented shared extension: optional `IShaderBackendCachePolicy` interface
with `UseSharedCache` (false opts out).
A backend opting out bypasses both lookup and store. Existing backends retain
their current behavior. Xbox initially opts out; no global cache rewrite or
PS3 behavior change. Tests must use a cache that throws on any call to prove
bypass is before lookup, and a counting backend to prove repeated requests
compile again. These tests now pass, alongside default/explicit-true cache-hit
tests and failure-followed-by-success coverage. The actual Xbox backend remains
pending and must implement this opt-out before registration.

## Request preservation

The shaderc frontend now needs in-memory source, arbitrary entry point, defines,
options and shaderc include callbacks. The shared resolver takes requestingFile
and includePath, returning resolved Path and Source. Shaderc must determine active
includes; a regex scan cannot reproduce conditional preprocessing. If using a
Python process adapter, include callbacks require a bounded request/response
protocol to the C# resolver, or an equivalent preprocessor step using that
resolver. Merely copying includes from disk would bypass the shared contract.

The existing lowerer remains the single implementation. Its explicit physical
layout is not part of ShaderCompileRequest: the backend must derive and validate
it from actual reflected interfaces and its supported vertex/resource ABI.
Do not infer a layout from asset/program names or reuse a fixture unconditionally.
Logical binding-policy shifts are not Xenos physical fetch indices.

Current lowerer `_uniform_value` / `_constant_vector` also require uploaded
vec4 values in the diagnostic layout, although emitted operands reference
constant registers (`Lane("c", index, lane)`) rather than embedding those values.
Before exposing a general backend, separate symbolic uniform layout/capacity
from per-draw values. Do not fabricate material values to satisfy the probe API.
Keep the probe upload-value validation in its diagnostic caller. This is a
promotion step for the same lowerer, not a second compiler implementation.

The validated fixtures explicitly annotate vertex/varying locations with
`[[vk::location(...)]]`. Current SPIR-V parsing retains locations, not a complete
authored HLSL semantic-to-stream contract. The backend must publish and validate
its accepted location/shape ABI (or add actual semantic reflection), rather than
assume arbitrary authored declaration order is compatible. This is especially
important for VS/PS linking and two same-shaped vertex attributes.

`ShaderCompileResult` requires non-null program definition, binary and diagnostic
list even on failure. Backend capabilities express model/stages/ray tracing only;
they do not imply all HLSL operations or Standard variants are supported. Publish
vertex/pixel only and retain explicit diagnostics for unsupported lowering.

## Separation from subsequent gates

Phase 2 can expose a compiler backend without enabling material rendering.
Keep target registration separate from selecting it automatically for legacy
Xbox material cooks: enabling a target provider prematurely could route the
existing Standard requests into the deliberately restricted compiler and break
the previously working scene before the new artifact/runtime path exists.
Phase 3 must freeze the artifact ABI and validate it in C#/C before cooking
depends on the payload. Phase 4 must declare actual shader dependencies;
Phase 5 must load/bind them. No legacy shader-name substitution is acceptable.

## Evidence

Graph-first Verify lookup in C-dev-helworks-helengine, generation
2026-09-23T13:53:46Z. Exact-path coverage reports metadata_changed, so the parent
read enum, descriptors, backend, request, result, capabilities, include result,
binding policy, identity and service source directly. PS3 compiler acceptance
documentation states its shared cache/backend/runtime integration remains a
separate milestone. The user has been asked whether that agent is still editing
these shared contracts; the subsequent answer and committed-state check are
recorded above.
