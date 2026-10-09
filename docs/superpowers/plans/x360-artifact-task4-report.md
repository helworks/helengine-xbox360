# X360 artifact Task 4 report

Status: DONE

## Checkpoint

Task4 implementation is scoped to the approved brief's exporter project and
program, integration tests, `docs/x360-shader-abi.md`, and edits to
`tests/shader_artifact_test.c` and `tools/test-shader-artifact.ps1`. The existing
test helper may be extended only if required. Shared Engine/PS3 sources,
existing runtime call sites, and all unrelated dirty checkout work remain
outside this task.

The checkout already contains extensive unrelated and prior-task changes. The
pre-task snapshot is retained at `.artifacts/x360-artifact-task4-before/`.
`ArtifactExport.csproj`, `Program.cs`,
`Xbox360ShaderArtifactIntegrationTests.cs`, and `docs/x360-shader-abi.md` were
absent at preflight. The approved design and wire appendix were read in full;
the Task3 report confirms that the envelope already verifies the artifact's
big-endian stage digests, so Task4 will test that behavior without duplicating
it.

## Evidence ledger

- Approved Task4 brief and design/wire appendix: read before implementation.
- Existing host baseline supplied by the parent run: full .NET 107 passed;
  Python 108 passed; parent `-Suite All` host, ASan, UBSan, and four PowerPC
  object builds exited 0 at
  `.artifacts/shader-artifact/20260925T212042282Z`.
- Initial preimplementation missing-exporter RED was not captured and is not
  claimed. The real postimplementation REDs below are retained and identified
  by their actual failing check.

### First real integration RED and current stage

- The first exporter execution reached the batch compiler assertion and
  reported register counts `5/4` against the brief's `45/15` expectations.
  Inspecting validated snapshots showed `45/15` are microcode word counts;
  the exporter check now asserts the right snapshot fields.
- The exporter then produced the real pair files and snapshot-derived manifest:
  mesh cost `110`; Batch2D word counts `45/15` and costs `94/94`.
- Command:
  `powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite Integration`
  reached the pinned Docker host C executable and failed at
  `hel_x3sp_parse(bytes, size, &view, &error) == HEL_X3SP_ERROR_OK` for the
  first exported artifact (exit `134`, assertion abort). This is the actual
  integration RED; it is not an environment or build failure. Parser error
  code/offset diagnostics are being added to identify the failing invariant.
- A diagnostic rerun reached the same parser rejection with
  `code=6 (HEL_X3SP_ERROR_SEMANTIC)`, `offset=1184`, artifact size `1500`:
  `.artifacts/shader-artifact/20260925T214112338Z/integration/mesh.x3sp`.
  Its directory places resource section kind 5 at offset `1172`; offset `1184`
  is the first resource's logical-set field. The first record is the real
  `Transform` cbuffer with logical set `0`, logical slot `17`, physical set and
  binding `0`, absent fetch slot, and VS bank `0`. The semantic validator
  reports this grouped field when its condition rejects either logical set or
  logical slot; direct source inspection confirms it rejects `slot != 0`.
- The shared factory's real compiler snapshot has `LogicalSlot=17` under the
  approved `ShaderBindingPolicy` shift, and the existing builder test asserts
  this. The serializer preserves that value. The wire specifies preservation of
  logical set/slot and fixes the cbuffer's logical/physical set and physical
  binding; the reader's extra logical-slot-zero condition rejects this valid
  shifted snapshot. The export uses the original shift so this mismatch remains
  visible. No Task1/3 source has been edited.
- The parent authorized a narrow Task3 extension in
  `src/platform/xenia_guest/x360_shader_artifact_semantics.c`; a before snapshot
  is at `.artifacts/x360-artifact-task4-before/src/platform/xenia_guest/x360_shader_artifact_semantics.c`.
  The repaired-integrity rich C fixture now carries VS cbuffer logical slot 17
  and PS slot 300. Before the parser change,
  `powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite Semantic`
  failed as intended: `code=6 offset=884`; offset 884 is resource record 0's
  logical-set/slot group, and the semantic condition rejected its nonzero
  logical slot. The approved one-condition change removed only `slot != 0`; all
  set, physical binding, fetch, and bank checks remain. The focused Semantic run
  then passed host C, ASan, UBSan, and four PPC object compiles at
  `.artifacts/shader-artifact/20260925T214706360Z`.
- With that reader correction, the real integration advanced to a separate
  Task1 writer mismatch. The mesh parser reports `code=6 offset=1136` in
  `.artifacts/shader-artifact/20260925T214722544Z/integration/mesh.x3sp`.
  Kind 4 starts at 848; record index 8 starts at 1136 and is the PS `LOCATION0`
  color output. The snapshot-derived manifest reports its varying slot as `0`
  (`INTERFACE\t1\tO\t1\t0\t4\t0...`), but the wire requires the PS output slot
  to be absent (`0xffffffff`). `Xbox360ShaderProgramFactory.BuildOutputSnapshots`
  assigns the PS location as a physical slot, and the serializer writes it
  verbatim. The Task4-owned C# test independently reproduced this:
  `dotnet test ... --filter FullyQualifiedName~Legacy_mesh_pair_serializes_real_compiler_snapshots_at_110_dwords`
  failed with expected `4294967295`, actual `0`.
- The parent approved a second narrow source extension and saved the original
  at `.artifacts/x360-artifact-task4-before/builder/shaders/Xbox360ShaderArtifactSerializer.cs`.
  `WriteInterfaces` now writes `Absent` only for PS outputs. It leaves the
  factory snapshot, VS outputs, and PS inputs unchanged. The exporter manifest
  independently applies the normative rule: pixel-stage index 1 + output has no wire slot,
  so its TSV slot is `-1`; it does not inspect serialized bytes or call the
  production serializer helper. The factory's PS output `PhysicalSlot=0` is
  source metadata, while the wire field is absent (`0xffffffff`).
- After adding UTF-8/resource cases to the controlled C fixture, the Semantic
  suite exposed a fixture-construction issue: `code=2 offset=1202` was the
  first nonzero byte in the final section's required zero padding. Compacting
  the access table and moving the string pool left two stale source-pool bytes
  after the shortened pool. The helper now zeroes only the new padding range;
  this corrects the fixture and does not change parser acceptance.
  The next run reached a separate repaired-integrity hidden-tail mutation: it
  appended four bytes to the declared artifact but enlarged the final pool
  section by only one, so the envelope correctly rejected the size mismatch
  (`code=2 offset=1228`) before the intended semantic check. The controlled
  mutation now expands the pool length/count by all four appended bytes, so
  the test exercises the hidden-payload semantic rule rather than a malformed
  envelope.
- Real compiler export then reached the C manifest comparator. During test
  bring-up, its independent absent-slot mapping first keyed off the wrong stage
  index (corrected to pixel index `1`), and a comparator column parsed absent
  `USED` matrix stride (`-1`) as unsigned (corrected to signed comparison,
  matching wire stride `0`). These were Task4 harness issues; no artifact bytes
  were used to produce the expected manifest.
- The attempted runs and generated artifacts are retained under
  `.artifacts/shader-artifact/20260925T214014968Z/`,
  `.artifacts/shader-artifact/20260925T214052516Z/`, and
  `.artifacts/shader-artifact/20260925T214112338Z/`; no files were removed.
- Final full acceptance:
  - `powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite All`
    exited 0 at `.artifacts/shader-artifact/20260925T220106808Z`. Envelope,
    semantic/accessor and real integration C tests passed; host ASan/UBSan runs
    passed; all four freestanding PowerPC objects compiled. The fixed-seed
    bounded semantic mutation corpus (512 iterations) runs in the semantic
    test. Retained logs, binaries, and four PPC objects are in that directory.
  - Full .NET suite: 109/109 passed. The command and output are retained in
    `20260925T220106808Z/full-dotnet-tests.log`.
  - Full Python suite: 108/108 passed. The command and output are retained in
    `20260925T220106808Z/full-python-tests.log`.
  - Standalone real integration run also exited 0 at
    `.artifacts/shader-artifact/20260925T220002719Z`.
- Real compiler export contains three artifacts and a snapshot-derived
  `expected.txt` (6,606 bytes). `mesh.x3sp` is 1,500 bytes; its VS/PS
  microcode word counts are 51/21 and upload cost is 110 dwords.
  `batch-unoptimized.x3sp` is 1,444 bytes and
  `batch-optimized.x3sp` is 1,440 bytes; each has 45/15 VS/PS words and a
  94-dword upload cost. Costs are read from compiler snapshots and verified
  against the parsed C reader view.
- Final scope review against the saved snapshots confirms exactly two
  cross-task production-source deltas: the approved removal of the cbuffer
  logical-slot-zero restriction in
  `src/platform/xenia_guest/x360_shader_artifact_semantics.c`, preserving its
  set/physical-binding/fetch/bank checks; and the approved PS-output-only
  `Absent` slot mapping in
  `builder/shaders/Xbox360ShaderArtifactSerializer.cs`, preserving the source
  snapshot, VS outputs, and PS inputs. Both have focused regression coverage and
  passed the final suite.
- The inline Task4 checklist review found no remaining acceptance gaps in the
  exporter/oracle boundary, C field-by-field comparisons, controlled fixtures,
  deterministic serialization, harness isolation, or ABI documentation. No
  reviewer subagent was used, honoring the explicit no-subagents instruction.
- Limitations: digest validation establishes integrity, not authenticity;
  full Xenos ISA validation, GPU execution, emulator, console, cooker, and
  runtime loading are not covered. Compiler/C-parser agreement is host-only.
