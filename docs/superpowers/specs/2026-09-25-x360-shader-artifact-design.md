# Xbox 360 shader pair artifact and pure C reader

Status: specification, wire appendix and implementation plan approved by the
user; all four artifact tasks implemented, host-verified and scoped-reviewed.
Runtime/GPU integration remains out of scope. Its concrete wire appendix is:
`2026-09-25-x360-shader-artifact-wire.md`.

## Intent and scope

Carry the validated host VS/PS pair into a portable, deterministic artifact
that the Xbox 360 can inspect safely before any GPU command is emitted. Reuse
the existing immutable program snapshots, physical layouts and pair linker.
Support the proven Mesh3D and shared SpriteBatch Textured Batch2D routes.

This slice introduces the C# writer, a pure C reader, a documented versioned
wire contract and cross-language fixtures. It does not connect the cooker,
material loader, renderer or command ring. No shared Engine/PS3 source edits,
worktrees, commits, emulator launches, captures or desktop automation.
RoundedShape and Standard lowering remain unsupported where the compiler
currently rejects them; serialization never substitutes another shader.

The earlier SpriteBatch amendment listed RoundedShape before ABI work. This
slice proposes doing the artifact first, as approved in the subsequent chat:
explicit layout support is ready, while no RoundedShape capability is claimed.
Any future feature outside the v1 contract requires a versioned extension.

## Architecture and alternatives

Selected: one file containing exactly one vertex program and one pixel program,
their bindings/interfaces and their pair identity. This lets validation succeed
or fail for the whole pair and avoids dangling stage-file references.

Separate stage files would improve stage-level deduplication but require a
manifest and additional lifetime/reference handling. Embedding serialized host
JSON would avoid a binary table schema but add a JSON dependency and a larger
validation surface to the guest. Neither is part of this slice.

The writer consumes an explicit nonempty shader asset ID plus an already
validated `Xbox360ShaderProgramPair`. The asset ID is supplied by the caller,
not inferred from a source path. Program labels, entrypoints and variant remain
data, never selectors for a hardcoded shader. The writer returns owned bytes;
filesystem publication and staging belong to the future cooker.

## Envelope and byte order

All numeric file fields and microcode words are explicitly little-endian.
Writer input already exposes logical uint32 words decoded from compiler
big-endian bytecode. Serialize those words, not the original byte array.
The PowerPC reader decodes individual bytes; never cast file bytes to native
structs or uint32 pointers, even if offsets happen to be aligned.

The fixed envelope is 64 bytes:

| Byte offset | Field | Contract |
| --- | --- | --- |
| 0 | magic, 4 bytes | ASCII `X3SP` |
| 4 | version, u32 | 1 |
| 8 | header size, u32 | 64 |
| 12 | total size, u32 | Exact input length, at most 1 MiB |
| 16 | target, u32 | 8 (Xbox360) |
| 20 | section count, u32 | Exact number required by the v1 directory schema |
| 24 | directory offset, u32 | 64 |
| 28 | flags, u32 | 0; unknown bits rejected |
| 32 | integrity, 32 bytes | SHA-256 over the entire file with bytes32..63 treated as zero |

Directory records are four u32 values: section kind, byte offset, byte length,
record count. Required kinds have fixed IDs and record sizes in the wire-layout
appendix produced before implementation. Exactly one instance of each required
kind; unknown kinds and duplicate kinds are rejected in v1. Empty required
tables still have directory entries with zero count/length. Sections appear in
kind order at four-byte-aligned offsets, with zero padding, no overlaps or
unreferenced trailing bytes. All references use file offsets, not pointers.

The implementation plan must include the complete fixed-record layout appendix
before delegating writer/reader implementation: field offsets, enum values,
sentinels, size limits and at least one hand-derived byte example. Both languages
must implement that same appendix; neither implementation defines the format
implicitly. Header or table changes after review must update this specification.

## Required payload

The directory separates pair metadata, two stage records, microcode words,
interfaces, resources, declared constant members, used constant accesses and
UTF-8 strings. Records reference bounded ranges in the relevant tables.

Pair metadata preserves the explicit asset ID, variant and tool identity.
Stage records preserve stage, program label, entrypoint, compile identity,
original bytecode SHA-256, register count and selected vertex profile.
Tool/compile/bytecode digests are 32-byte values parsed from validated hexadecimal
host identities. Source paths are omitted: they are diagnostic host information,
not guest identity or a substitute for an asset ID.

Keep a profile value per stage, as the host API does. Only the VS profile
determines vertex-fetch validation. A PS profile is compile provenance, not a
second vertex buffer contract; the current linker does not require profile
equality. Preserve that behavior rather than introducing an implicit new rule.

Interfaces preserve kind, location, width, physical varying slot and, where
applicable, vertex fetch constant, stride, offset and format. Resources preserve
name/type, logical set/slot, physical set/binding, optional fetch slot and bank.
Logical shifts must not be confused with physical bindings.

Constant buffers preserve declared byte size, register capacity and bank base;
declared members preserve name/type/offset/size. Used-access records separately
preserve member association, register range and matrix major/stride. Do not
invent orientation for unused matrices or synthesize material/draw values.

Strings use length-delimited strict UTF-8, no embedded NUL, at most 4096 bytes
each and 256 KiB in total. Empty variant is permitted when the host permits it;
asset ID, program label, entrypoint and resource/member names are nonempty.
No Unicode normalization or culture-dependent sorting. Asset ID is retained as
opaque caller-supplied text, not parsed as a GUID without a shared contract.

Serialize tables deterministically: VS before PS; interfaces by kind/location;
resources by type/logical set/slot; declarations and accesses by offset/name.
String interning and references follow this canonical traversal. Equal inputs
produce equal bytes regardless of dictionary enumeration order.

Program control and upload costs are derived, not trusted commands. Do not put
arbitrary PM4 packets in the file. The reader recomputes these values from the
validated records; if redundant derived fields are included in the appendix,
they must match exactly or parsing fails.

## Reader validation and ownership

The pure C API takes a const byte buffer and explicit length. It performs no
allocation, I/O, logging, ring writes or platform initialization. It returns a
status/error location and populates a view only after complete validation;
failure leaves the output view cleared. Diagnostics distinguish envelope,
integrity, range, unsupported value and semantic incompatibility failures.

Views borrow the original bytes. The caller must keep the buffer alive and
unchanged while any view/accessor is in use. Accessors decode words and bounded
records, returning values or byte spans, never unaligned native typed pointers.
This is not a defense against a caller concurrently mutating its own buffer.

Validation order: minimum header and size cap; envelope/directory bounds;
integrity; all table ranges, strings and references; stage semantics; pair
compatibility. Validate ranges using subtraction/division checks before addition
or multiplication, including on a 32-bit target. Bound every loop by a validated
count and file size. Reject aliasing table ownership where a record must belong
to exactly one stage, duplicate interfaces/resources and inconsistent references.

Semantic limits mirror the existing host factory, including:

- Exactly VS then PS, target8; register counts1..64; each microcode size1..1024
  dwords and divisible by3. The existing zero-register shader limitation remains
  an explicit failure, not a serializer normalization.
- Mesh3D vf0/stride12, locations0..3 widths3/3/2/4 and offsets0/3/6/8.
  Batch2D vf0/stride24, locations0..5 float4 at offsets0/4/8/12/16/20.
  Sparse active input subsets retain the full physical stride.
- VS position float4; location varyings0..7; PS inputs match VS outputs by
  location/width/slot; PS has the supported single float4 output.
- At most one constant buffer per stage, at physical b0/set0, size at most4096,
  16-byte padding; separate bank bases0/256; exact supported member types,
  alignment, sizes, nonoverlapping ranges and validated used-register ranges.
- Optional pixel texture/sampler pair only, tf1 distinct from vertex vf0.
  Unknown resource types, formats and optional-field encodings fail.
- Matching variant/tool identity and the current pair-linking rules.

Recompute each stage bytecode SHA-256 over the logical words encoded back into
compiler big-endian byte order, separately from the artifact integrity hash.
SHA-256 verifies integrity, not authenticity, sandboxing or complete ISA safety.
The parser must never advertise a checksum as proof that arbitrary microcode
is safe to execute. Full ISA verification is outside this slice.

Use a dependency-free, separately tested SHA-256 helper only if no suitable
helper exists in the actual reader dependency boundary. Test standard known
vectors, multiblock input and the virtual-zero integrity-field convention.
No platform crypto/library dependency may be added silently.

## Acceptance tests

1. One checked-in hand-derived binary fixture and textual field/byte manifest
   consumed by C# and C, not generated anew by the serializer under test.
   Include values above255 and deliberately unaligned input-buffer placement.
2. Real Mesh3D and shared Textured Batch2D pairs serialize deterministically and
   are read by C. Compare all preserved fields, words, hashes and derived costs.
   Existing pair expectations remain110 and94 dwords respectively.
3. Cover sparse vertex locations, separate VS/PS constants, used versus unused
   declarations, nonzero logical shifts, UTF-8 names and differing PS provenance
   profiles with compatible interfaces.
4. Reject every truncation of fixtures, oversized files/counts, overflow ranges,
   overlapping/duplicate/unknown sections, illegal padding/strings/references,
   wrong target/stages/layouts, resource/constant inconsistencies and mismatched
   varyings. Recompute integrity after semantic mutations so those tests reach
   semantic validation rather than merely failing a checksum.
5. Reject corruption of microcode and metadata; test stage bytecode hash mismatch
   even with valid artifact integrity. Verify failed parse clears the view.
6. Execute host C tests with sanitizers when supported; compile the same parser
   for PowerPC. Use the existing pinned Docker toolchain if native tools remain
   unavailable, without downloading a new image. Record actual commands/results.
7. Run all local Python/.NET suites. No test skips count as successful coverage
   for required compiler/C prerequisites. No Xenia/hardware execution claim.

## Delivery boundaries

Expected local files: C# artifact/writer and tests, C reader/header and test
harness, binary/text fixtures, wire-format documentation and a reproducible
test command. Existing runtime call sites and generated engine sources remain
untouched. New code stays off the production path until later integration gates.

Proceed to a detailed implementation plan only after this written specification
is approved. Luna remains the requested implementer, with explicit ownership and
task-scoped reviews; any independent whole-branch review needs separate approval.
