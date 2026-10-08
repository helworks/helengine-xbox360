# MeshMaterialV1 completion — 2026-09-26

Status: implementation and verification complete; scoped review approved.
User authorized finishing the incomplete 3D stage after a read-only audit.
Existing dirty work was preserved; before snapshots for this follow-up are in
`.artifacts/mesh-material-finish-before`. No commits, worktrees, shared-source
edits, emulator launch or screen automation.

## Actual failures and scoped corrections

1. Fresh PowerPC regression reproduced the missing PlatformMaterialAsset.Dispose
   at X360RenderManager3D.cpp:321. The generated type and its base expose a
   destructor, not Dispose. Remove that call while retaining delete and stream
   cleanup. The ShaderMaterialAsset branch keeps its real Dispose method.
   RED: `.artifacts/shader-runtime/20260926T150654594Z`.
   GREEN: `20260926T150810852Z`, including host/shared-writer sanitizers,
   adapter and entire generated-core PowerPC compilation.
2. The old C runtime test omitted the newly added pixel_constants field from
   its HelBatchDraw initializer, failing -Werror before any runtime test.
   Explicitly initialize the absent Batch2D pixel bank to zero.
3. The original mesh-material.hlsl could not compile: the RowMajor SPIR-V
   matrix path used dp4 with a literal homogeneous lane. Four numerical REDs
   reproduced this for w=0/1 with optimization on/off. The lowerer now uses
   masked row accumulations for those literals without allocating fake uniform
   constants. The existing all-register dp4 path remains unchanged. The original
   shader fixture is unchanged; generated microcode executes against a
   non-symmetric matrix and hand-calculated outputs in the Python executor.
4. A pixel shader exporting a cbuffer color directly reported zero registers,
   inconsistent with SQ_PROGRAM_CNTL's count-minus-one and the managed 1..64
   contract. A focused test failed with 0 != 1. Metadata now reserves the
   minimum slot and highest index0, with unchanged microcode and register field0.
5. The Engine row-vector matrix packing was reversed for shaderc's transposed
   SPIR-V representation. Default HLSL column-major is SPIR-V RowMajor here,
   so the uploaded vec4s contain Engine matrix columns. Both mesh and Batch2D
   packers now transpose for RowMajor and copy for ColMajor. Independent literal
   packed matrices caught the previous error; the old test mirrored the same
   faulty conditional. Mesh RED: `20260926T151522328Z`; Batch2D RED:
   `20260926T151638638Z`, under `.artifacts/shader-artifact`.
6. The managed rejection test mutated its original dictionary, but requests
   snapshot field values. After the compiler fixes its last assertion failed
   because it still submitted the old supported request. Construct a new
   request for StandardShader; no production validation was weakened.

The first added content test failed because the export staged only Textured
(`20260926T150956906Z`, exit134). The test exporter now also compiles and stages
the real MeshMaterialV1 shader. An intermediate exporter compile failed for a
missing Results namespace import; that setup error was corrected directly.

## Coverage added

- Real cooker -> declared manifest -> HELC archive -> identity/layout-checked
  runtime opening of MeshMaterialV1 alongside shared Textured.
- Independently hashed tuple path; wrong asset and wrong layout rejection.
- Known matrix packing; complete VS bank0 and PS bank256, exact supplied color,
  insufficient capacity/wrong constant count with zero writes.
- Real mesh creation and draw-command capture, invalid mesh/matrix rejection,
  insufficient ring budget with zero writes, legacy shader-cache invalidation,
  vertex fetch stride and indexed draw emission.
- Numerical execution of emitted vertex microcode with homogeneous0/1,
  optimized/unoptimized; direct uniform pixel export minimum register budget.

## Verification

- Full .NET: **114 passed, 0 failed, 0 skipped**.
  `dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --nologo --verbosity quiet`
- Full Python: **107 passed** in9.299s (previous integration baseline106 +1).
  `python -m unittest discover -s tests -p '*_test.py'`
- Final C All: exit0, host/ASan/UBSan including new mesh draw and content cases,
  six reader/runtime PowerPC objects. Logs:
  `.artifacts/shader-artifact/20260926T151902942Z`.
  `powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite All`
- Generated core: existing `.artifacts/mesh-shader-generation-cooked/generated`
  (412 sources), freshly compiled with build-core.ps1 to
  `.artifacts/mesh-material-finish-core/libhelcore.a`, zero failures.
- Full image built with tools/build.ps1, the above core/generated tree and
  `.artifacts/shader-artifact/20260926T151711142Z/staged` as content input.
  Two cooked shader entries; stub scene catalog; HEL_DEMO_MODE=0.
  `.artifacts/mesh-material-finish-image/helengine.xex`, static checks0failures.
  SHA256: `4B84963E629A4E7FB402631C1769F6AA3687AFF2CE6CBF88824C4A69BF4BEFAC`.

Existing shared-source CS0414/IDE0040 warnings and the host archive assembly's
missing GNU-stack warning remain visible. No warning suppression was added.

Scoped read-only review approved the before/after diff with no Critical or
Important findings. One Minor observation concerned a comment suggesting
snapshotting coverage; clarified that the test checks unsupported selection,
not the shared request constructor itself. This final edit is comment-only.
GPU execution and unrelated dirty work were explicitly excluded from review.

This is host and compile/link validation, not GPU rendering proof. The image
contains shader fixtures but no authored test scene/material catalog. Existing
MeshMaterialV1 constraints remain opaque, unlit, textureless, first-material
only. RoundedShape lowering, Standard, arbitrary uniforms and textured 3D are
not enabled. Do not launch this XEX without explicit user authorization.
