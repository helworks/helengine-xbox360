# Artifact Task2 brief

Parent preflight: Docker requires elevated execution in this environment; image
already installed, --pull=never --network none succeeds. Host gcc12.2.0.
PowerPC compiler is /opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc15.2.0;
it is not on default bash PATH (existing tools/build.ps1 exports that directory).
Host libasan.so/libubsan.so paths resolve; actual runtime probe remains yours.
Graph SHA256 src search returned0; coverage known assembly/inc gaps paginated;
bounded rg of src/platform/xenia_guest and src/engine found no SHA256/OpenSSL/
mbedtls helper. No usable dependency in this reader boundary was discovered.

Read this first. Implement only this task; spec and wire appendix are approved.
Read both docs/superpowers/specs/2026-09-25-x360-shader-artifact-{design,wire}.md completely.
No subagents. Preserve other agents' edits in this dirty checkout. Use apply_patch and TDD.
Write full evidence/report to docs/superpowers/plans/x360-artifact-task2-report.md.
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


## Task 2 â€” Pure C integrity and envelope validation

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
