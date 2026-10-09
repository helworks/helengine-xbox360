# Task 2 — bounded SPIR-V to Xenos lowering

## Result and ownership

Implemented a CPU-verified transform/color lowering slice in `tools/shader-probe/`
and its focused tests/fixtures. The frontend-owned `shader_probe.py` and
`tests/shader_frontend_test.py` were not edited. Builder/runtime files and
concurrent changes were left untouched. The earlier checkpoint was requested
by the parent, not by the user.

The task-owned orphan `tests/shader_probe_test.py` was removed after checking
its expectations: it targeted an abandoned `probe` API and expected texture
success, neither compatible with this bounded CLI. Frontend/parser validation,
lowering successes and explicit errors, and both CLI input routes are covered
by the maintained frontend and lowering suites.

## Implemented

- `spirv_parser.py` bounds-checks module/instruction word counts and uses the
  Khronos SPIR-V 1.6 revision 7 grammar for opcode numbers. In particular,
  `ImageWrite=99`, `FAdd=129`, `FSub=131`, `FDiv=136`,
  `VectorTimesMatrix=144`, and `MatrixTimesVector=145`. GLSL.std.450 is pinned
  to version 100 revision 2. The parser rejects unknown opcodes and malformed
  instruction boundaries.
- `lower_xenos.py` lowers the actual tested straight-line float32 transform,
  vertex color/UV, uniform pixel color, and interpolated-color arithmetic.
  It carries matrix layout decorations through uniform access/load and accepts
  only the documented RowMajor + VectorTimesMatrix and ColMajor +
  MatrixTimesVector combinations with float4x4 and 16-byte stride. FDiv,
  texture/sampler storage, control flow, unsupported operations, capabilities,
  decorations, and resource layouts fail explicitly. It also rejects
  unsupported varying qualifiers, duplicate varying slots, duplicate output
  aliases, and vertex shaders without BuiltIn Position.
- Uniform metadata records actual uploaded float4 values, consecutive register
  order, member byte offsets, matrix major/stride, and raw-memory-order
  convention. Input/varying maps, register count, highest register index,
  SQ_PROGRAM_CNTL field value, and big-endian binary dword order are distinct
  metadata fields. The final real exec is marked `exece`.
- `xenos_probe.py` accepts authored HLSL through the stable installed-shaderc
  frontend API or precompiled SPIR-V, plus explicit stage/entry/layout and
  output prefix. It emits `.xasm`, `.bin`, and `.json`; hashes input, source
  where applicable, SPIR-V, layout, and shaderc DLL for HLSL. It guards output
  aliases and invalidates only explicit outputs on failure.
- `tests/shaders/fixtures/xenos_runtime_layout.json` describes one physical
  `vf0` vertex array: 12-dword stride, position dword 0, UV dword 6, color
  dword 8. Offsets/stride are dwords, not bytes.
- `tools/shader-probe/README.md` documents CLI invocation, layout conventions,
  supported/rejected scope, and the CPU/runtime validation boundary.

The numerical executor lives only in `tests/shader_lowering_test.py`; it decodes
the emitted binary's CF, fetch, ALU, swizzle, destination, mask, and export
fields. It uses one 12-dword physical vertex buffer and checks the nonsymmetric
matrix oracle `[30, 107, 211, 321]`, color/UV fetches, uniform PS output, and
shaderc-generated FAdd/FSub. Changing the HLSL expression changes the executed
pixel result. The executor fails on instructions outside its tested subset.

Review follow-ups: optional `OpLoad` memory-access operands are now rejected;
known literal decorations validate their exact arity in the parser; and matrix
`OpCompositeConstruct` now produces a deliberate `LoweringError`. The tests
mutate a real shaderc load/decorated module, exercise direct parser failures,
check matrix-construction rejection, and verify malformed `.spv` CLI input
leaves no artifacts. The executor rounds loaded values and each ALU result to
ordinary finite float32; its MAD path rounds the product before addition based
on the current Xenia translator reference. The `[16777216, 1, -16777216]`
arithmetic regression distinguishes this from Python double precision. These
tests verify algebraic routing for representable fixtures, not bit-exact GPU
behavior: denormals, NaNs, and all hardware rounding details are not modeled.

## Verification evidence

The parent independently ran the lowering suite before this CLI/documentation
completion: `python tests/shader_lowering_test.py -v` — 11 tests passed,
including numeric matrix/arithmetic assertions. Following the CLI additions,
the focused command initially ran 14 tests and found a test-only fixture setup
error (`ProbeCliTests` did not define the inherited shaderc library handle).
After making the test compile its fixture through the existing helper, the
focused command passed. An additional test now covers stale-output cleanup
when CLI shaderc setup is missing:

```text
python tests\shader_lowering_test.py -v
Ran 15 tests in 0.871s
OK
```

This covers both HLSL and SPIR-V CLI routes, output hashes, lowering-failure
cleanup, shaderc-setup-failure cleanup, and layout alias protection in addition
to numeric and rejection cases. The parent separately confirmed that HLSL and
precompiled-SPIR-V routes emitted identical transform binaries with SHA256
`40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF` in
`build/shader-probe-validation/{transform,from-spv}`. No emulator was run.

RED reproduced ignored `OpLoad` access operands, matrix-constructor `TypeError`,
and malformed-Location `IndexError` on the CLI route. After the narrow fixes:

```text
python tests\shader_lowering_test.py -v
Ran 20 tests in 1.170s
OK

python -m unittest discover -s tests -p '*_test.py' -v
Ran 46 tests in 2.825s
OK
```

No runtime or GPU test has been performed.

## Limits

This is a provisional CPU-side compiler probe, not runtime integration. Texture
sampling, division, control flow, arbitrary SPIR-V, extra/nonzero descriptor
resources, and unsupported semantic qualifiers/decorations remain explicit
errors. GPU/Xenia execution, runtime shader loading, final container format,
and device interpolation behavior remain unverified.
