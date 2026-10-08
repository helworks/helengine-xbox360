# Shared SpriteBatch: Xbox 360 design amendment

Status: amendment and first Textured implementation plan approved. Existing shader
pipeline design remains in force except that the fixed mesh vertex layout must
not become the only representable artifact layout. No runtime changes made.

## Intent and ownership

Use the shared SpriteBatch shader, variants and Batch2D core created alongside
the PS3 implementation. Do not create a second shader/catalog or modify the PS3
checkout. Preserve existing Xbox 360 mesh rendering and UI/text while building
the new path. No emulator, captures, foreground control, worktrees or commits.

## Current shared source contract

- `helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl`.
- `helengine/engine/helengine.shader/shaders/SpriteBatchShaderVariants.cs`:
  Textured (VS/PS, no rounded define), RoundedShape (VS/PS,
  HELENGINE_BATCH_ROUNDED=1).
- `helengine/engine/helengine.core/managers/rendering/batch2d/Batch2DVertex.cs`:
  Position, TexLocal, Color, Shape, Corners, BorderColor, all float4.
- PS3 renderer explicitly asserts 96-byte stride and member byte offsets
  0,16,32,48,64,80. That is shared CPU vertex data, not a PS3 GPU artifact ABI.
- VS uses BatchCameraBuffer/projection at b0. Textured PS uses t0/s0;
  RoundedShape PS uses no texture and contains conditional distance/coverage
  and border computations. Reuse authored semantics, including straight alpha.

Source SHA256 observed 2026-09-25:
`3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2`.
Source may change concurrently; revalidate before implementing tests.

## Actual Xbox 360 compilation probe

Passed the unmodified shared source through the existing real-shaderc Python
protocol harness, VS/PS entrypoints, SM4.0, exact variant defines, with both
optimize=false and optimize=true. No source or compiler files were modified.
Protocol process failures are shader rejection, not test successes. The outer
probe command returning zero does not mean all shaders compiled.

| Variant/stage | optimize=false | optimize=true |
| --- | --- | --- |
| Textured VS | input location0 requires float3, received float4 | cbuffer/struct missing OpName |
| Textured PS | compiled, register_count4, tf1 | resource variable missing OpName |
| RoundedShape VS | input location0 requires float3, received float4 | cbuffer/struct missing OpName |
| RoundedShape PS | unsupported SPIR-V opcode184 | unsupported SPIR-V opcode184 |

These are the first reported failures, not an exhaustive opcode inventory.
Host compilation is not proof of Xenia or hardware execution.

Graph evidence: shared Engine index generation2026-09-23T13:53:46Z did not track
the two new shader/catalog files; Batch2D symbol search also had no results.
Used bounded file discovery and read their current sources. PS3 generation
2026-09-25T16:29:24Z found cooker/tests, but coverage marked metadata_changed;
read those sources and the renderer directly. No inference of absence from
missing graph nodes. The codebase-memory skill file was inaccessible; MCP
tool instructions supplied the graph-first/freshness fallback workflow.

## Options

1. Freeze the mesh-only artifact now: simplest immediate serializer, but would
   require a new layout contract before shared SpriteBatch could use it.
2. Infer layout from shader names: rejected; fragile and violates the existing
   rule that names do not select hardcoded shader behavior.
3. Recommended: explicit validated vertex-layout description supplied by the
   Xbox 360 host path, represented in program metadata and the future artifact.
   Keep the existing mesh profile and add the shared Batch2D profile. Select
   deliberately, never by filename or incidental input subset.

## Proposed sequence and acceptance

1. Pin tests to the current shared catalog/source without copying HLSL. Diagnose
   optimized reflection name loss and preserve required identity/reflection
   explicitly; do not invent names or silently disable requested optimization.
2. Add explicit Xbox 360 vertex-layout selection through the local compiler
   boundary and validate it against active reflected inputs. Preserve mesh vf0,
   stride12 dwords and offsets. Batch2D has stride24 dwords, float4 payloads at
   offsets0,4,8,12,16,20; verify actual reflected locations/semantics before
   assigning fetch mappings. Keep tf1 disjoint from vf0. Do not change shared
   compile contracts until a demonstrated need is separately coordinated.
3. Prove the real Textured VS/PS pair compiles and links with shared projection,
   UV/tint and resources. Add negative layout tests and keep legacy binary hashes
   and existing tests unchanged. This is the first bounded implementation slice.
4. Inventory and implement the actual RoundedShape lowering requirements with
   numeric tests for corners, border widths, alpha and thresholds. Reject it
   explicitly until supported; do not replace it with textured quads or simplify
   its shader silently. No claim that opcode184 is its only missing operation.
5. Freeze serializer/C-reader ABI only with explicit layout representation;
   retain validated stages/resources, identities, integrity/range checks and
   pair budget from the original plan. Connect to shared Batch2D through a local
   Xbox 360 sink in a subsequent runtime step, preserving the legacy UI route
   until validation. Xenia execution remains separately authorized.

The first slice does not promise complete RoundedShape support or render-system
activation. The upstream zero-register uniform-only PS limitation remains a
separate recorded constraint. This amendment does not adopt PS3 P3SP binaries:
shared source/core data are reusable, GPU microcode and command submission are not.
