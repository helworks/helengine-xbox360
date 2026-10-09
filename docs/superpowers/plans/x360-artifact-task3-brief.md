# Artifact Task3 brief

Task2 accepted. Reuse exact internal header/SHA/envelope APIs now on disk;
move shared error enum/types into public header once, no duplicate definitions.
Parent observed host+ASAN+UBSAN+PPC pass. Existing harness uses base64 transport
for PowerShell->bash, /tmp sanitizer executables, non-PIE ASAN, bounded probes
and tests; preserve these environment adaptations. Extend it to persist actual
sanitizer test logs to /out (currently only probe logs survive container exit),
including failure output. This satisfies the plan's retained evidence requirement.
No full ISA validation: enforce record/register/interface restrictions only.

Read this first. Implement only this task; spec and wire appendix are approved.
Read both docs/superpowers/specs/2026-09-25-x360-shader-artifact-{design,wire}.md completely.
No subagents. Preserve other agents' edits in this dirty checkout. Use apply_patch and TDD.
Write full evidence/report to docs/superpowers/plans/x360-artifact-task3-report.md.
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


## Task 3 â€” Semantic validation and borrowed-view accessors

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
