# Phase 3 reader/serializer preflight (2026-09-24)

These notes are implementation evidence, not a frozen file-format specification.
Phase3a first normalizes host compiler output and links a pair; its exact public
model is reviewed before assigning binary offsets. No reader is connected to
the runtime, no material capability is enabled, and no emulator is controlled.

## Verified consumers and constraints

- `shader.c` uploads logical uint32 words through PM4_IM_LOAD_IMMEDIATE. Each
  stage costs three overhead dwords (packet header/type/count), plus code.
  Existing validation checks triple-word alignment and a few first-CF fields;
  it is not a general microcode verifier. The artifact parser must not claim
  full ISA validation from these checks or a checksum.
- `shader_gpu_probe.inc` uses constant bank base0 for VS and256 for PS. Every
  nonempty constant upload costs two overhead words; values must come from
  material/draw data later, not fabricated in the artifact's symbolic layout.
- Five SQ register writes cost ten words. Program register fields are highest
  register indices, not counts; interpolator field is highest allocated VS
  varying slot. Actual counts remain separate in the host model.
- `video.c` ring capacity is16384dwords but safe per-frame allowance is8192.
  There is no public remaining-capacity API in the inspected ring interface.
  Pair upload cost cannot by itself prove a whole frame fits. Runtime integration
  will need an explicit preflight/reservation before writes, without changing
  the safe allowance or treating an overflow flag after partial emission as success.
- Actual transform/textured pair:51VS/21PS codewords; VS9registers(field8),
  PS5registers(field4), two interpolators(field1). Combined SQ_PROGRAM_CNTL
  0x00100408. Program78 +constant22 +SQ10 =110 upload words. This excludes
  fetch state, draws, framebuffer state, UI and all other frame commands.

## Compiler metadata versus artifact responsibilities

- Phase3a verification exposed a real uniform-only pixel shader whose lowerer
  metadata reports register_count=0. The current validated host contract requires
  1..64 and rejects it. Resolve this upstream encoding/capacity question before
  claiming that such shaders are supported by the artifact/cooker path; do not
  normalize the count silently in the serializer or use the controlled test
  header as evidence that this shader executes on Xenia or hardware.

- The compiled binary is big-endian uint32 bytes. The approved design proposes
  explicit little-endian integers in the future file; a writer must convert
  words, and the PowerPC reader must decode bytes instead of casting the file.
- Native uniforms list USED accesses; shared reflection lists DECLARED members.
  An unused matrix has no used-access major/stride entry. Never invent one or
  silently confuse declaration count with upload capacity.
- Typed logical resource slots include request shifts. Raw bindings, constant
  bank, vertex vf0 and texture tf1 are different values. Preserve them separately.
- `PlatformShaderArtifactCookRequest` already supplies complete resolved sources
  keyed by `ShaderAssetId`; `PlatformShaderDependency` carries asset ID plus
  vertex program, pixel program and variant. Source path is not that asset ID.
  The serializer/cooker must receive the asset identity explicitly rather than
  guessing it from filename or storing the source path as a fake asset GUID.
- Hash checking establishes integrity/identity, not authenticity or a proof
  that arbitrary third-party shader instructions are safe. Source/compile/tool
  identities are opaque on the guest; recomputing payload integrity is separate.
- The future pure C parser must validate the complete envelope, ranges and
  tables before returning a usable view; no ring/PM4 function belongs in it.
  Input-buffer ownership and lifetime must be explicit for any zero-copy view.

## Verification environment

Graph Verify scope: X360 generation2026-09-24T13:01:53Z; relevant paths were
metadata_changed and were read directly. Shared Engine index was older and
the exact artifact request/dependency files were likewise read directly.
No gcc/clang on the current Windows PATH; Docker is available. The already
installed image digest was verified without download:
`devkitpro/devkitppc@sha256:db8e39c73546d461d5606dc7dfed462a7d5957498502562e3b46769d02496cc1`.
The existing GPU-runtime harness uses its host gcc and can provide the pattern
for future pure-C artifact tests. Reader tests must also compile for PowerPC;
cross-compilation alone does not establish parser execution on the console.

The bounded filename/content search in xenia_guest found no SHA256/CRC helper
to reuse; this is not a repository-wide absence claim. Before introducing a
hash implementation, check the chosen reader's actual dependency boundary.
