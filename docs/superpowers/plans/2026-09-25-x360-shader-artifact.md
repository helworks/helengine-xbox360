# Xbox 360 Shader Pair Artifact Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development task-by-task. Preserve the requested Luna implementation method. User approved the plan and wire appendix; implementation is underway.

**Goal:** Deterministically serialize validated Mesh3D/Batch2D shader pairs and validate the same bytes through a pure C reader, without enabling the runtime path.

**Architecture:** C# writes X3SP v1 from immutable program pairs plus an explicit asset ID. A bounded, allocation-free C parser validates the entire file before exposing borrowed views. Writer and reader are joined by independent golden bytes and real compiler-produced fixtures, not a new shared runtime dependency.

**Tech Stack:** .NET9/C#, C11, SHA-256, existing Python compiler, existing pinned Docker host/PowerPC toolchain.

**Spec:** `../specs/2026-09-25-x360-shader-artifact-design.md` (approved); exact wire appendix `../specs/2026-09-25-x360-shader-artifact-wire.md` (review with this plan).

## Execution status

Tasks 1 through 4 are implemented and passed scoped review. This artifact slice
is complete; cooker/runtime integration and GPU execution remain out of scope.
The detailed execution record, including actual commands, review corrections
and the Task 3 preimplementation RED evidence limitation, is in
`x360-artifact-progress.md` and each `x360-artifact-taskN-report.md`.
The original checklists below are retained as the approved acceptance plan;
they are not a claim that every planned process step occurred without deviation.

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

## Preflight and ownership

Prior completed slice:95 .NET/108 Python tests, scoped reviews passed. Re-run
before changing code and record current results; those counts are not a promise
about the present checkout. Current recorded HEAD is
`ac5be3d7e9e2e3b4e37378853e45b70ae19ca9ad`; verify instead of resetting it.
The checkout contains significant prior uncommitted work. Save task-specific
before snapshots for existing files; review only each task's delta.

Graph search found `Xbox360ShaderProgramFactory`; coverage on2026-09-25 reported
metadata_changed. Direct reads confirmed 16-byte member alignment, contiguous
used-register ranges, resource fields and profile-dependent fetch checks. Tool
identity/binary hashes are validated by the factory. Revalidate relevant paths
at execution; a missing graph result never proves absence.

| Task | Creates/owns | Shared interface/dependency |
| --- | --- | --- |
| 1 | C# artifact/writer, golden fixture, writer tests | Wire appendix, existing immutable snapshots |
| 2 | private C SHA/envelope and C test harness | Golden bytes and exact directory schema from1 |
| 3 | public C semantic reader/accessors and parser tests | Validated envelope/SHA from2; writer schema from1 |
| 4 | real fixture exporter and cross-language acceptance | Writer from1, reader from3; no production changes |

Tasks are sequential. Each task gets a fresh Luna implementer and scoped review;
no worker subagents or overlapping file writers. Use Luna high for1/4, xhigh
for2/3 because bounds/semantic validation need more judgment. Explicitly specify
model and reasoning effort. Escalate capability only after a demonstrated need.

## Task 1 — Deterministic managed writer and independent golden fixture

**Create:**
- `builder/shaders/Xbox360ShaderArtifact.cs`: immutable asset ID/pair association.
- `builder/shaders/Xbox360ShaderArtifactSerializer.cs`: deterministic encoding.
- `builder/shaders/Xbox360ShaderArtifactWire.cs`: local constants and checked byte helpers; no shared Engine enums on wire.
- `builder.tests/Xbox360ShaderArtifactSerializerTests.cs`.
- `builder.tests/Xbox360ShaderArtifactTestSupport.cs`: controlled test-only snapshot construction, fixture path and independent field reads.
- `tests/shaders/artifacts/minimal-v1.hex`, `minimal-v1.x3sp`, `minimal-v1.md`.
- `tools/shader-probe/materialize_artifact_fixture.py`: literal hex conversion/hash verification only; no production serializer import.

**Consumes:** `Xbox360ShaderProgramPair`, its stage snapshots, both specification files.
**Produces:**

```csharp
public sealed class Xbox360ShaderArtifact {
    public Xbox360ShaderArtifact(string assetId, Xbox360ShaderProgramPair programs);
    public string AssetId { get; }
    public Xbox360ShaderProgramPair Programs { get; }
}
public static class Xbox360ShaderArtifactSerializer {
    public static byte[] Serialize(Xbox360ShaderArtifact artifact);
}
```

- [ ] Read all record definitions. Check that every snapshot field is preserved,
  deterministically derived or explicitly omitted (source path only). Map enum
  values explicitly; validate strict UTF-8 with `new UTF8Encoding(false,true)`.
- [ ] Author the literal fixture from the appendix's668-byte recipe. Resolve
  stage/file hashes using Python `hashlib` over independently specified bytes.
  Materializer defaults to verification, not overwrite. Its explicit `--write`
  mode materializes the binary from reviewed hex; ordinary tests only read it.
  Record byte offsets, digest preimages/results and synthetic non-GPU limitation.
- [ ] Write RED tests. Use a test-only reflection helper for the minimal snapshot,
  not a new public mutable constructor or production fixture fallback:

```csharp
[Fact]
public void Minimal_pair_matches_independent_wire_bytes() {
    var artifact = Xbox360ShaderArtifactTestSupport.MinimalArtifact();
    byte[] expected = File.ReadAllBytes(Xbox360ShaderArtifactTestSupport.GoldenPath);
    byte[] actual = Xbox360ShaderArtifactSerializer.Serialize(artifact);
    Assert.Equal(668, actual.Length);
    Assert.Equal(expected, actual);
    Assert.Equal(new byte[] { 0x78, 0x56, 0x34, 0x12 }, actual[560..564]);
}
```

- [ ] Run the focused command below and capture missing-API RED before adding code.
- [ ] Implement in two passes: canonical record/string collection and checked
  size/offset assignment; then exact-sized buffer emission. Use checked arithmetic
  before allocation, `BinaryPrimitives.WriteUInt32LittleEndian`, raw decoded digest
  bytes, zero reserved/padding fields and `SHA256.HashData` for integrity. Input
  snapshots must remain unchanged. Return a fresh independent array each call.
- [ ] Add tests for empty/null/too-long/NUL/invalid-surrogate asset and labels;
  empty variant; multibyte strings near4096-byte limit; reused full strings versus
  substrings; collection permutations; null arguments and rejected file-size cap.
  A synthetic oversized-input builder may use reflection only inside tests.
- [ ] Assert all profile/resources/constants fields by independent little-endian
  test reads, including PS profile differing from VS, logical shifts above255,
  bank256, used range versus unused matrix and both matrix orientations. Verify
  stage hashes against original BE words, not LE file slices. No C# production
  deserializer is required just to test the serializer.
- [ ] Run focused and full.NET suites; verify golden hex/binary consistency.
  Report RED/GREEN and exact files. No commit.

```powershell
dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --filter FullyQualifiedName~Xbox360ShaderArtifactSerializerTests
python tools/shader-probe/materialize_artifact_fixture.py --verify
```

## Task 2 — Pure C integrity and envelope validation

**Create:** `src/platform/xenia_guest/x360_shader_artifact_internal.h`,
`x360_shader_artifact_envelope.c`, `x360_shader_artifact_sha256.h/.c` in the
same directory; `tests/shader_artifact_envelope_test.c`;
`tools/test-shader-artifact.ps1`.

**Consumes:** wire appendix and unchanged golden binary from1.
**Produces:** private `HelX3spSection { uint32_t offset,length,count; }`,
`HelX3spEnvelope { const uint8_t *data; size_t size; HelX3spSection sections[8]; }`,
`HelX3spError { uint32_t code,offset; }` and these internal interfaces:

```c
int hel_x3sp_validate_envelope(const uint8_t *data, size_t size,
                             HelX3spEnvelope *out, HelX3spError *error);
uint32_t hel_x3sp_read_le32(const uint8_t *p); /* only after range validation */
/* Streaming SHA supports a virtual zero field and BE word digest inputs. */
void hel_x3sp_sha256_init(HelX3spSha256 *ctx);
void hel_x3sp_sha256_update(HelX3spSha256 *ctx, const uint8_t *data, size_t size);
void hel_x3sp_sha256_final(HelX3spSha256 *ctx, uint8_t digest[32]);
```

Define error codes once:0=OK,1=argument,2=envelope,3=integrity,4=range,
5=unsupported,6=semantic. Functions return0 on success or that error code;
error output optional, view output required. Whole-file errors offset0;
field errors absolute byte offset. Null input/output rejects safely; clear
non-null output before any validation. The SHA context stays private and
contains fixed-size state/block storage, never heap pointers.

- [ ] Check actual reader dependency boundary for an existing usable SHA helper;
  graph first, bounded direct read if stale/missing. If none, implement the local
  helper above; do not pull packages, OS crypto or code with unclear licensing.
- [ ] Add RED C tests for empty/abc/multiblock SHA vectors, split-update boundaries
  at55/56/63/64/65 bytes, and input immutability. Known abc SHA:
  `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
- [ ] Implement/test SHA then envelope parsing. Check count and size before
  indexing; range form is `offset <= size && length <= size-offset`.
  Validate multiplication with `count <= remaining/record_size` first.
- [ ] Add golden envelope test at buffer alignment+1, all668 truncations, total
  size mismatch, oversized declared size, bad flags/version/target, section order,
  overlap, nonzero padding and count/length overflow. Explicitly test repaired
  checksums where earlier structural gates would otherwise hide the intended error.

```c
HelX3spEnvelope view;
HelX3spError error;
memset(&view, 0xA5, sizeof view);
assert(hel_x3sp_validate_envelope(bytes + 1, golden_size, &view, &error) == 0);
assert(view.sections[2].offset == 560);
/* Each failure must leave every published view field zero. */
```

- [ ] Verify stage digests in this layer after validating both stage word ranges:
  stream each logical LE word as four BE bytes. Never mutate input or allocate
  an entire transformed code buffer. Semantic stage restrictions belong to3.
- [ ] Build harness using pinned image
  `devkitpro/devkitppc@sha256:db8e39c73546d461d5606dc7dfed462a7d5957498502562e3b46769d02496cc1`,
  `docker run --pull=never --rm`, network disabled, source read-only mount and
  a separate checkout `.artifacts/shader-artifact` output mount. Validate resolved
  output path stays inside checkout. No shell-built deletion commands.
- [ ] Run host C11 with `-Wall -Wextra -Werror`; probe sanitizers once and run
  address/undefined sanitizers if available. Report an unavailable sanitizer
  distinctly, not a passed sanitizer run. Compile objects with powerpc-eabi-gcc
  and `-ffreestanding`; no new runtime link/build call sites.
- [ ] Record commands, environment limitations and SHA/envelope test results.
  Envelope success is not a public usable shader view yet. No commit.

```powershell
powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite Envelope
```

## Task 3 — Semantic validation and borrowed-view accessors

**Create:** `src/platform/xenia_guest/x360_shader_artifact.h/.c`,
`x360_shader_artifact_semantics.c`; `tests/shader_artifact_test.c`.
**Modify:** internal header and test harness from2 only as required for full suite.

**Consumes:** envelope/SHA helpers from2 and immutable X3SP schema.
**Produces:** public header with `HelX3spSection`, `HelX3spError` moved from
private header (single definition), and:

```c
typedef struct { const uint8_t *data; size_t size; } HelX3spSpan;
typedef struct {
    const uint8_t *data; size_t size;
    HelX3spSection sections[8];
    uint32_t program_control, interpolator_count;
    uint32_t program_upload_dwords, constant_upload_dwords;
    uint32_t state_upload_dwords, pair_upload_dwords;
} HelX3spView;
int hel_x3sp_parse(const uint8_t *data, size_t size,
                  HelX3spView *out, HelX3spError *error);
int hel_x3sp_record(const HelX3spView *view, uint32_t kind, uint32_t index,
                   HelX3spSpan *out);
int hel_x3sp_string(const HelX3spView *view, uint32_t offset, uint32_t length,
                   HelX3spSpan *out);
int hel_x3sp_word(const HelX3spView *view, uint32_t stage_index,
                 uint32_t word_index, uint32_t *out);
int hel_x3sp_span_u32(HelX3spSpan span, size_t offset, uint32_t *out);
```

`stage_index` is0/1,section kinds1..8. Record spans are decoded through checked
`span_u32`; no native struct casts. Word accessor decodes LE. String accessor
returns a bounded byte span (not a NUL-terminated C string), permits `(0,0)`,
checks UTF-8/length bounds for caller-supplied ranges. Accessors require a valid
unmodified parsed view, guard argument/range errors and clear provided outputs
on failure. They do not authenticate forged views or concurrent buffer mutation.

- [ ] Write RED tests: parse golden succeeds with control0 and cost22; invalid
  nonempty table reference inside wrong section fails after valid integrity.
- [ ] Implement a local temporary envelope/view and validate fully before copying
  to output. Iterate tables in stage ownership order; enforce all enum/reserved/
  count/layout/constant/resource rules from appendix. Never clamp invalid values.
- [ ] Validate strict UTF-8 including C0/C1 controls other than NUL as valid UTF-8
  data (no invented identifier policy), surrogate/overlong/out-of-range rejection.
  Enforce canonical whole-string reuse and complete pool coverage in <=522 refs;
  bounded rescans avoid an unbounded stack array/heap dependency.
- [ ] Validate declarations by16-byte alignment, exact sizes and nonoverlap;
  used references by same-stage ownership, unique declaration, exact consecutive
  register range and matrix decorations. Names compare bytes, not locale.
- [ ] Validate interfaces/resources, then pair linking. Recompute control/cost;
  do not expose a reader API that emits PM4 or promises whole-frame fit.
- [ ] Add mutations with repaired file hashes: unknown profile, bank swap, slot
  vf0/tf1 mix, wrong widths/strides/offsets, missing position/color, varying
  mismatch, duplicate resource/name, matrix stride/major mismatch, declaration
  overlap, used access alias, foreign-stage reference, malformed strings and
  hidden trailing records. Check specific error class and cleared view.
- [ ] Test accessor first/last/one-past indexes, nulls, huge offsets and alignment+1;
  every fixture truncation; original input unchanged on success and failure.
- [ ] Run full C harness including sanitizers and PPC compilation, then record
  focused review evidence. No claim that PPC compile executes on the console.

```powershell
powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite All
```

## Task 4 — Real-pair cross-language acceptance and delivery

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

## Self-review and handoff

Coverage: envelope/word encoding/identities ->1/2; complete record appendix and
fixtures ->1; ownership/bounds/UTF-8/resources/constants/linking ->3; real-pair
interop and reproducibility ->4. Five review-focus risks all have named tests.
New API signatures are defined above; wire values live in one linked appendix.
Tasks2/3 share private headers/harness and therefore execute sequentially.
No existing material/runtime capability is enabled by completing any task.

The implementation ledger must record source hashes, commands/results, changes
in scope and reviewer findings. Before each fresh implementer, snapshot the
owned existing files. No independent final review without asking the user.
Review this plan and appendix before dispatching Luna.
