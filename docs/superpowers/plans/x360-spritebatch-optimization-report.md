# Task1 — optimized reflection names report

## Result

Optimized shaderc compilation now preserves the actual resource and uniform
member names needed by reflection. No names are synthesized, and missing names
still produce explicit reflection errors.

The shared SpriteBatch source was read directly from
`C:/dev/helworks/helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl`.
Its SHA-256 before and after this work was
`3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2`.

## TDD evidence

Initial real-compiler RED:

```text
python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k test_optimized_textured_spritebatch_reflection_preserves_resource_names
Ran 1 test — FAILED: resource variable %17 has no OpName
```

Expanded optimized reflection RED:

```text
python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k optimized_
Ran 3 tests — 5 failures
```

Those failures reproduced stripped SpriteBatch resource names, stripped
constant-buffer and member names, missing names on optimized include resources,
and the parser rejecting shaderc's `OpNoLine` (317) when caller debug metadata
was enabled.

Focused GREEN runs:

```text
python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k optimized_
Ran 3 tests — OK

python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k test_debug_line_metadata_does_not_hide_unsupported_runtime_operations
Ran 1 test — OK

python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k 'round_trip'
Ran 2 tests — OK; both legacy microcode hashes match
```

The exact required full-suite command also passed:

```text
python -m unittest discover -s tests -p '*_test.py'
Ran 96 tests — OK
```

## Effective policy and implementation

`compile_hlsl_from_source` has a separate internal
`preserve_reflection_names` option. The host protocol enables it when the
request has `optimize:true`; shaderc therefore emits debug metadata when either
the caller requests `debug_info:true` or optimized reflection needs names.
The request flags remain unchanged in the compile identity. Optimization still
uses shaderc performance level 2 whenever `optimize:true`; it is not disabled.
The intermediate SPIR-V is not returned.

Shaderc's optimized debug output contains `OpLine` (8) and `OpNoLine` (317).
The parser recognizes `OpNoLine`, and Xenos lowering skips only those two
source-location directives. A real `discard` shader still fails on unsupported
runtime `OpKill` (252), confirming that executable instructions were not
whitelisted to accommodate metadata. Required resource and member names continue
to fail explicitly if shaderc does not provide them.

## Files changed

- `tools/shader-probe/shader_probe.py`
- `tools/shader-probe/backend_protocol.py`
- `tools/shader-probe/spirv_parser.py`
- `tools/shader-probe/lower_xenos.py`
- `tests/shader_backend_protocol_test.py`
- `docs/x360-shader-host-protocol.md`

## Scope and limitations

Tests cover the shared Textured pixel entrypoint with both `debug_info` values,
optimized uniform-member reflection with both values, optimized include-based
resource reflection, and explicit rejection of absent required names. Task2
layout selection and Task3 C# integration remain outside this change. No
emulator or runtime validation was performed.

## Scoped review fix — exact debug-directive arity

Review found that minimum-operand validation allowed malformed `OpLine` and
`OpNoLine` instructions to reach the lowerer's metadata skip. Exact arity is now
checked in the parser before either directive can be treated as metadata:
`OpLine` requires 3 operands and `OpNoLine` requires 0.

RED:

```text
python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k test_spirv_line_rejects_extra_operands
Ran 1 test — FAILED: SpirvError not raised

python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k test_spirv_noline_rejects_any_operands
Ran 1 test — FAILED: SpirvError not raised
```

GREEN:

```text
python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k test_spirv_
Ran 3 tests — OK (valid OpLine and OpNoLine, plus both malformed cases)

python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k test_debug_line_metadata_does_not_hide_unsupported_runtime_operations
Ran 1 test — OK; valid directives are accepted and OpKill remains rejected

python -m unittest discover -s tests -p 'shader_backend_protocol_test.py' -k test_optimized_textured_spritebatch_reflection_preserves_resource_names
Ran 1 test — OK (both debug_info subcases)
```

The full Python and .NET suites were not repeated for this scoped parser fix;
the parent-reported 96 Python and 80 .NET results remain the prior baseline.
