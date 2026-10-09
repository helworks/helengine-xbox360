# Phase 2d host protocol/layout report

## Result

Implemented the one-shot JSON-lines shader host and compiled-SPIR-V reflected
layout under the frozen protocol 1 schema. The C# integration entrypoint is
`tools/shader-probe/backend_protocol.py`; launch it as
`python -u <script> --shaderc <absolute shaderc_shared.dll path>`. The source
checkout script and the builder bundle layout both resolve `xenos_asm.py` from
the parent `tools` / `shader-tools` directory.

The host uses the existing `shader_probe.compile_hlsl_from_source` and
`lower_xenos.lower_spirv` APIs. It performs no disk include lookup, emits
include requests only when shaderc asks for an active include, derives the
`vf0`/varying/uniform/texture layout from SPIR-V, and retains native descriptor
indices in reflection and lowerer metadata. Logical binding shifts remain in
the request and compile identity for the managed consumer to apply. No Python
frontend/parser/lowerer/assembler file or C# file was edited. No emulator/UI was
used and no commit or worktree was created.

## RED / GREEN

- Initial RED: `python tests/shader_backend_protocol_test.py -v` ran before the
  executable existed. The subprocess reported that
  `tools/shader-probe/backend_protocol.py` could not be opened and returned no
  protocol response; all four initial protocol cases failed at that missing
  entrypoint (the no-response assertion also exposed a test-harness error,
  corrected before GREEN).
- Bundle lookup RED: the new path test failed with `ImportError` because
  `_tool_source_paths` did not exist yet. GREEN after deriving the assembler
  from `backend_protocol.py`'s parent `shader-tools` directory.
- Output-line-limit RED: the focused limit test failed with `ImportError` for
  `_write_protocol_line`. GREEN after applying the 8 MiB bound to outbound
  protocol lines as well as inbound lines.
- Initial focused GREEN: `python tests/shader_backend_protocol_test.py -v` — 12
  tests passed. The tests use the installed shaderc DLL at
  `C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll`.
- Initial full GREEN (after the outbound-line boundary adjustment):
  `python -m unittest discover -s tests -p '*_test.py' -v` — 89 tests passed in
  17.884 seconds, including all 12 focused host-protocol tests.
- Whitespace check: `git diff --check` exited successfully. Git printed only
  line-ending normalization warnings for pre-existing modified files.

## Integration evidence

- Real shaderc transform VS binary SHA-256:
  `40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF` —
  matches the Phase 2c baseline.
- Real shaderc textured PS binary SHA-256:
  `D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A` —
  matches the Phase 2c baseline.
- Reflection verifies actual vertex/pixel interface locations and formats,
  BuiltIn Position ID 0, cbuffer name `Transform`, member names/types/offsets,
  80-byte padded size, five symbolic float4 registers, and matrix major/stride.
- Texture and sampler names and raw set/binding are reflected from compiled
  SPIR-V; the compiled sample maps that actual pair to physical `tf1`.
- Include tests cover nested and conditional requests, actual supplied-content
  hashes, changed include identity, inactive branches, missing content, and
  mismatched response IDs. Failures return no binary.
- Supported source-expression changes alter emitted microcode. Option and
  variant changes alter compile identity; variant changes preserve the same
  binary when the compiled program is unchanged. Tool identity includes ABI,
  shaderc bytes, and the six functional Python files named in the brief.
- Tests cover malformed JSON/request data, missing shaderc, the 8 MiB line
  bound, empty/unaligned/over-1024-dword microcode, and bundled tool lookup.

## Changed paths

- `tools/shader-probe/backend_protocol.py`
- `tools/shader-probe/backend_layout.py`
- `tests/shader_backend_protocol_test.py`
- `docs/x360-shader-host-protocol.md`
- `docs/superpowers/plans/x360-shader-host-protocol-report.md`

The full existing Python suite stayed green. C# process lifecycle and managed
binding-shift application remain the worker's integration scope; this report
does not claim a completed managed build or runtime/emulator validation.

## Review follow-up

- Storage-class RED: the focused allowlist test initially failed for storage
  classes 5, 8, 10, 11, and 13, which reflection had silently skipped; its
  diagnostic expectation also distinguished the old messages for 4, 9, and
  12. GREEN after allowing only nonresource classes 1/3/6/7 and explicitly
  rejecting every other unhandled class.
- Timeout RED: the hanging-child fixture failed because `run_protocol` had no
  deadline-capable script seam and its synchronous stdout read could block.
  GREEN after moving stdout reads to a queue-backed reader thread, applying one
  monotonic deadline across messages and process exit, and terminating and
  waiting for only the child started by the helper.
- Follow-up focused GREEN: `python tests/shader_backend_protocol_test.py -v` —
  14 tests passed.
- Follow-up full GREEN: `python -m unittest discover -s tests -p '*_test.py'
  -v` — 91 tests passed in 11.793 seconds.

The allowlist concerns reflection's resource enumeration. The existing
lowerer's separate storage-class restrictions remain authoritative; this
follow-up did not broaden lowering support.
