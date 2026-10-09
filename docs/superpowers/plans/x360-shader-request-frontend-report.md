# Phase 2a: compiler request preservation report

## Status

DONE_WITH_CONCERNS. The in-memory frontend and its real-shaderc tests are
implemented. Downstream Xenos lowering compatibility remains bounded by the
existing lowerer and was not changed here; frontend options can produce SPIR-V
that the lowerer may reject. The lowerer's existing uniform-value coupling is
a separate later promotion step. GPU/runtime evidence and shared Engine work
remain with the parent task.

## TDD evidence

- RED: `python tests/shader_request_frontend_test.py -v` — 8 tests ran and
  failed with `AttributeError: module 'shader_probe' has no attribute
  'compile_hlsl_from_source'`, demonstrating the requested API was missing.
- GREEN: `python tests/shader_request_frontend_test.py -v` — 9 tests passed
  after implementation (the additional fixture test covers both transform and
  textured defaults).
- Review RED: `python tests/shader_request_frontend_test.py -v` — the new
  native-signature assertion failed because the Python binding declared two
  arguments for `shaderc_compile_options_set_warnings_as_errors`.
- Review GREEN: `python tests/shader_request_frontend_test.py -v` — all 10
  tests passed after correcting the binding and the result annotation.
- Final suite: `python -m unittest discover -s tests -p '*_test.py' -v` —
  70 tests passed in 10.562s after the review correction.

The first implementation attempt also exposed ctypes' callback restriction on
returning a pointer-to-structure directly. The resolver callback now returns
the ABI-compatible opaque pointer value, and the real DLL exercises allocation,
release, nested resolution, and failure callbacks.

The shaderc public header confirms
`shaderc_compile_options_set_warnings_as_errors(shaderc_compile_options_t)`
takes only the options handle. The binding now matches that one-argument
signature; the existing real-DLL warning-as-error test continues to verify
that warnings cause compilation failure. The result annotation also now names
`IncludedSource` records to match runtime values.

## Changed paths

- `tools/shader-probe/shader_probe.py`
- `tools/shader-probe/README.md`
- `tests/shader_request_frontend_test.py`
- `docs/superpowers/plans/x360-shader-request-frontend-report.md`

## API contract

`compile_hlsl_from_source(library, source_bytes, source_name, stage,
entrypoint, defines=None, include_resolver=None, *, debug_info=False,
optimize=False, warnings_as_errors=False)` compiles in-memory HLSL through
shaderc. `include_resolver(requesting_source_name, requested_include_name)`
returns `(resolved_source_name, source_bytes)`; `None` means missing. The
resolver is invoked by shaderc only for active includes. Nested requests use
the resolved name of their parent include.

The call returns `ShaderCompileResult(spirv, diagnostics, included_sources)`.
Compilation failures have `spirv=None` and retain diagnostics; successful
compilation also retains shaderc warnings. Each `included_sources` entry is an
`IncludedSource(source_name, sha256)` describing the effective resolver bytes.
The existing `compile_hlsl(library, source_path, stage, entrypoint)` still
returns SPIR-V bytes and raises `ValueError` for shaderc compilation failures.
Its default SPIR-V bytes compare equal to the in-memory API for both the
transform and textured fixtures.

## Limits

- No implicit disk reads occur in the in-memory API.
- Includes are limited to 64 nesting levels. Python resolver exceptions and
  malformed responses are contained at the native callback boundary and
  surfaced in compile diagnostics.
- Source, diagnostic filename, entry point, macro names/values, resolved
  include filenames, and include payloads reject embedded NUL bytes. Macro
  names must be ASCII C-style identifiers, and define values must be strings.
- Stages remain `vertex` and `pixel`, matching this frontend.
- Debug info, performance optimization, and warnings-as-errors map to shaderc
  options. The real DLL tests verify changed SPIR-V or diagnostics for those
  settings. They do not expand lowerer support; the downstream lowerer remains
  responsible for rejecting unsupported modules.
- No cache or alternate lowering implementation was added.
