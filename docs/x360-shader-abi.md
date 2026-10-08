# Xbox 360 shader artifact ABI

The X3SP v1 file carries one validated Xbox 360 vertex/pixel pair: explicit
asset identity, compiler provenance, decoded microcode words, interfaces,
resource bindings, declared constants, used constant accesses, and pair upload
costs derived by the reader. The writer and pure C reader share a versioned
binary contract. The exact header, sections, record layouts, enum values,
sentinels, strings, ordering, validation rules, and hand-derived fixture are
defined in the [approved X3SP v1 wire appendix](superpowers/specs/2026-09-25-x360-shader-artifact-wire.md).

All scalar fields and stored words use little-endian encoding. Stage bytecode
digests are computed over the logical words in the compiler's original
big-endian byte order; the file integrity digest uses the virtual-zero header
integrity field. The PowerPC reader decodes bytes explicitly and exposes a
borrowed view only after validating the entire file.

The artifact format preserves host compiler records; it does not validate the
complete Xenos instruction set, authenticate its producer, emit GPU commands,
or establish hardware correctness. The separate cooker/runtime adapter now
consumes this format; its tests and limits are described in
[the integration report](superpowers/plans/2026-09-26-x360-cooker-runtime.md).
The checked-in minimal fixture and controlled contract fixtures are synthetic
format tests and must not be executed on a GPU. The real SpriteBatch
Textured/legacy artifact integration run checks host compiler output and C reader
agreement. The added runtime suites exercise cooked content lookup, bounded
command capture and generated shared batching on the host, and compile/link for
PowerPC. None of these tests establishes Xenia or console rendering correctness.
