# Artifact Task4 brief

Parent preflight: shared SpriteBatch source SHA256 remains
3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2.
Shared catalog still Textured(VS,PS,empty defines),RoundedShape(rounded define).
Task1 Xbox360ShaderArtifactTestSupport.cs already contains controlled rich
both-stage constants/unused matrices/shifted resources/Unicode/mixed profiles.
Reuse test-only fixture helpers where practical, without changing production
constructors or duplicating shared HLSL/catalog. Exporter's three real output
files stay real compiler results; synthetic fixtures remain clearly separate.
Task2's Docker harness adaptations (/tmp sanitized binaries, non-PIE ASAN,
base64 script transport, bounded runs) must survive the integration extension.

Read this first. Implement only this task; spec and wire appendix are approved.
Read both docs/superpowers/specs/2026-09-25-x360-shader-artifact-{design,wire}.md completely.
No subagents. Preserve other agents' edits in this dirty checkout. Use apply_patch and TDD.
Write full evidence/report to docs/superpowers/plans/x360-artifact-task4-report.md.
Do not mark report DONE until changes/tests are final; no edits after final handoff.

## Global Constraints

- Existing checkout only; no worktrees, commits, emulator, captures or desktop automation.
- Preserve unrelated dirty work. Shared Engine/PS3 sources and existing runtime call sites are read-only.
- One file contains exactly one VS and one PS; target8, version1, little-endian integers and microcode words, explicit decoding on PowerPC.
- Maximum file1048576 bytes; header64, directory8 records of16 bytes; section layouts/sentinels/order exactly as the wire appendix.
- Writer consumes explicit asset ID and validated snapshots; never infer asset identity/profile/shader behavior from filenames.
- Keep declared and used constants separate, VS/PS banks0/256 and vf0/tf1 independent. Do not invent constant values or unused matrix orientation.
- No cooker, material loader, ring emission, new dependencies, RoundedShape/Standard capability claim or GPU validation claim.
- Pure C parser: no allocation/I/O/logging/platform calls. Input stays alive and immutable while a view exists. Output view cleared on failure.
- Hashes mean integrity, not authenticity or full ISA validation. No raw C/C# struct serialization.
- Independent whole-branch review requires separate user approval; task-scoped reviews are part of this workflow.

## Review Focus

1. Valid-checksum malicious metadata must reach and fail semantic validation, not merely checksum validation (Tasks2/3).
2. A 32-bit or unaligned caller must never trigger overflowing arithmetic, unaligned loads or partial-view publication (Tasks2/3).
3. Compiler big-endian digest semantics must survive little-endian file encoding (Tasks1/2/4).
4. Sparse Batch2D inputs, unused matrix declarations and different PS profile provenance must not become accidental rejections or invented data (Tasks1/3/4).
5. Repeated strings must reuse exact whole spans; UTF-8 aliases, bad code points and hidden trailing payload must fail (Tasks1/3).


## Task 4 â€” Real-pair cross-language acceptance and delivery

**Create:** `tests/shaders/artifact-export/ArtifactExport.csproj`, `Program.cs`,
`builder.tests/Xbox360ShaderArtifactIntegrationTests.cs`,
`docs/x360-shader-abi.md` (link wire appendix; no competing offset definitions).
**Modify:** `tests/shader_artifact_test.c`, `tools/test-shader-artifact.ps1`;
local builder test helper only if required to reuse source paths.

**Consumes:** writer1/reader3; real shared SpriteBatch catalog/HLSL, legacy fixtures.
**Produces:** test-only exporter and reproducible end-to-end verification.
Exporter targetsnet9, references local builder and existing shared compilation
project without source edits; no new NuGet dependency. Interface:

```text
dotnet run --project tests/shaders/artifact-export/ArtifactExport.csproj --
  --output <absolute existing directory inside checkout>
  --shaderc <actual local DLL> --engine-root <shared engine checkout>
```

Outputs exactly `mesh.x3sp`, `batch-unoptimized.x3sp`, `batch-optimized.x3sp`
and `expected.txt` with tab-separated fields and hex strings for all preserved
records/words. Expected values come directly from compiler snapshots, not by
reading the serialized file. Host C test code consumes this manifest, compares
all fields/words through parser accessors and verifies costs110/94/94. Synthetic
coverage remains separate and labeled; no arbitrary manifest path from artifacts.

- [ ] Write RED integration assertions for exports before implementing exporter.
  Resolve original fixture HLSL from checkout and Textured source/catalog from
  shared engine; never copy shared shader/catalog or suppress prerequisite failure.
- [ ] Export validated legacy Mesh3D pair and shared Batch2D Textured with both
  optimization settings. Use explicit fixture asset IDs unrelated to source paths.
  Verify preserved BE bytecode hashes, layouts and expected counts45/15 for batch.
- [ ] Add controlled C#/C fixtures for both-stage buffers, unused matrices, sparse
  location5 Batch2D input, shifted logical resource slots, Unicode names and
  differing VS/PS profile provenance. Label them contract fixtures, not GPU proof.
  Their expected maps come from test descriptions, not reader output.
- [ ] Test repeated serialization byte equality, changed asset ID changes integrity
  but not code, and equivalent input ordering yields same file. Do not require
  optimized/unoptimized whole files identical: compile identities differ.
- [ ] Compare every semantic mutation outcome and error category through C tests;
  run sanitizer corpus with fixed seed and bounded iterations. Any crash is a
  failure; unsupported sanitizer environment is reported, not silently ignored.
- [ ] Extend harness to invoke exporter before read-only Docker source mounting;
  fail on missing image/compiler/.NET/shaderc/source or any nonzero exit. Preserve
  artifacts and logs; no automatic deletion of user data or external changes.
- [ ] Run full.NET, Python and C suites plus PPC compile. Document limits and
  host-only status; update progress and scoped review results. No commit or launch.

```powershell
dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore
python -m unittest discover -s tests -p '*_test.py'
powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite Integration
```

Use the same approved execution identity for.NET restore/build/test as prior
work if sandbox-generated files or stale NuGet paths block it. Do not edit
assets JSON or shared sources to hide environment problems. A new exporter
project needs an explicit restore before using `--no-restore` for it.
