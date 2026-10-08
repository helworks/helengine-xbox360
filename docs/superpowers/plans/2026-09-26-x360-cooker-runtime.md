# Xbox 360 cooker/runtime integration

## Follow-up status

The later MeshMaterialV1 continuation initially left the checkout failing to
compile. Its completion, additional compiler/runtime defects, regression
evidence and final build are recorded in
[the mesh completion report](2026-09-26-x360-mesh-material-completion.md).
The historical results below describe the earlier Textured integration, not
verification of that later unfinished mesh work.

User approved the integration outline on 2026-09-26. Work uses the existing
dirty checkout; no commits, worktrees, shared-source edits or GPU execution.

## Current contracts

- The editor merges IPlatformRendererShaderDependencyProvider dependencies and
  invokes IPlatformShaderArtifactBuilder with resolved source snapshots.
- X3SP v1 remains unchanged. Its reader owns integrity/semantic validation.
- SpriteBatch uses the shared Textured catalog, writer and 96-byte vertex ABI.
- Existing material cooking is opaque/unlit; Standard and RoundedShape shader
  support must not be inferred from this integration.
- The Engine, PS3 and original Xbox checkouts contain ongoing local work.
  Their current APIs are read-only dependencies.
- codebase-memory-mcp/Graphiti are unavailable in this session. Direct reads
  establish findings; no negative graph search or coverage claim is made.

## Implementation sequence

1. Add platform-local shader cooker and explicit dependency/path contract;
   compile all requested pairs before publication and declare exact outputs.
2. Load borrowed X3SP bytes from embedded content, checking requested identity
   and layout before exposing a runtime program.
3. Add bounded runtime uploads with caller-supplied constant values and host
   command capture tests, reusing the validated reader and ring primitives.
4. Connect shared Batch2D writer/emitter through an Xbox 360 sink and verify
   generated-core compatibility, vertex layout, camera and resource lifetime.
5. Run managed/Python/C sanitizer verification and PowerPC compilation.

Publication keys must distinguish asset, VS, PS and variant; labels never
select hardcoded microcode. Source snapshots are authoritative. Existing
compiler identities include tool/options/includes, and the cooker must not
reuse output based only on a source hash. Hash validation is not GPU proof.

## Status

Implemented locally:

- `Xbox360PlatformAssetBuilder` implements the existing optional cooker and
  renderer dependency interfaces. The renderer dependency is the shared
  `SpriteBatchShader.vs/.ps`, variant `Textured`.
- `Xbox360ShaderArtifactCooker` uses supplied source snapshots and the shared
  catalog, compiles validated pairs, and serializes X3SP v1. No cache is keyed
  on the caller's source hash. All compilation completes before file writes;
  each individual file is atomically replaced. A publication I/O failure fails
  the cook; publication of a whole multi-file request is not transactional.
  Existing files survive compile failure and no declarations are returned on
  failure. The editor must treat the failed cook as failed, as its contract does.
- Built-in Textured path: `shaders/spritebatch-textured.x3sp`. Other explicit
  Mesh3D/default selections use SHA256 over four BinaryWriter UTF-8 strings
  (asset, VS label, PS label, variant), preserving case and avoiding path input.
  Authored entrypoints are explicitly VS/PS for this slice. Unknown batch
  selections and Standard are rejected.
- The runtime borrows immutable embedded content. It checks asset, program
  names, variant and VS layout after the existing complete parser succeeds.
  The uploader consumes logical words and caller-supplied complete constant
  banks, checks capacity before its first write, and never repeats SHA checks.
- Textured rendering uses the generated shared writer/emitter and a 96-byte
  vertex ABI checked at compile time. Matrix packing uses the recorded used
  member major. Submission copies vertices/indices into the fenced UI arena,
  checks ring budget first, supplies viewport/scissor, and invalidates the
  legacy shader cache. Legacy draws restore their own program and scissor.
- Rounded rectangles retain their existing CPU tessellation path and flush
  pending shared batches. This does not implement RoundedShape shader lowering.
- At the first integration checkpoint generic Mesh3D cooking/loading/upload
  was available, with authored material binding still legacy. The later
  MeshMaterialV1 slice adds an explicit opaque/unlit, textureless binding for
  Transform.mvp and Material.baseColor; see the follow-up report. Arbitrary
  material semantics, Standard and textured 3D materials remain unsupported.
- The existing Engine already provides `HELENGINE_CODEGEN_FATAL_ONLY`.
  Selecting it in the two Xbox360 codegen profiles resolved the initial CPP1001
  failure. No Engine, PS3, Xbox original or codegen sources were edited.
- Text telemetry now counts submitted shared text chunks and glyph quads
  including effect passes. Sprite/text transitions flush to keep those counts
  attributable; consecutive draws of the same kind still batch.

## Verification and retained evidence

- Fresh pre-change baseline: 109 .NET tests passed.
- Managed cooker tests: real shared source compiled twice identically; bad
  source preserved existing output; selection and path identity cases passed.
  Full .NET suite: 113 passed, no skips. Existing shared-source warnings remain.
- Python: 106 passed. Two old text-source regex checks were replaced by the
  executable generated-writer test: 300 quads across 128-quad chunks, copied
  vertices, indices, ordering and camera flush. This is a coverage migration,
  not a claim that the old count of 108 still applies.
- `.artifacts/shader-artifact/20260926T132258244Z`: original envelope/semantic/
  mutation/real-pair suites plus runtime command capture and the production
  cooker -> manifest staging -> HELC archive -> content lookup -> runtime path
  passed host/ASan/UBSan. Six reader/runtime PowerPC objects compiled.
  The host archive assembly emitted a missing GNU-stack note warning; retained
  in tool output. It did not fail the tests.
- `.artifacts/shader-runtime/20260926T131653050Z`: generated writer passed host,
  ASan and UBSan; PowerPC runtime/adapters and entire generated core compiled.
- `.artifacts/cooker-runtime-image-final`: native image linked and XEX checks passed
  with one cooked shader in its archive and a stub scene catalog. No scene or
  GPU execution was performed. This is a build/link check, not a rendered scene.
  Final XEX SHA256:
  `F68588D894BAD727EE070268D7E22E48F9B636F55F89544EE98AACACEB876A83`.
  The final adapter resolves viewports through the shared CameraViewportResolver
  and uses the same rounded pixel edges for projection, viewport and scissor.
- Initial test setup failures are retained: cooker test root lookup overshot
  the checkout; generated writer test initially omitted RuntimeData.cpp; first
  core generation lacked the fatal-only symbol. Each was corrected directly.

Snapshots before modifications are in `.artifacts/cooker-runtime-before`.
No independent review session, commits, worktrees, emulator or capture tools
were used. The current integration has no GPU validation evidence.

## Reproduce

Set `HEL_X360_SHADERC` (or `SHADERC_SHARED_LIBRARY`) for editor cooking to the
existing shaderc library. The normal compiler options also support explicit
Python/protocol/library paths. Missing tools fail the cook.

Use a fresh output directory for `tools/generate-core.ps1`, passing the current
Engine and codegen checkout paths explicitly. `tools/test-shader-runtime.ps1
-GeneratedRoot <generated-directory>` runs the generated writer and compiles
the adapters/core using the pinned offline Docker image. The existing
`tools/test-shader-artifact.ps1 -Suite All` includes cooker/content/runtime
coverage with sanitizer execution in /tmp and non-PIE ASan.
