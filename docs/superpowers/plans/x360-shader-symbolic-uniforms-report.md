# Phase 2c symbolic uniform layout report

## RED / GREEN

- RED command: `python tests/shader_symbolic_uniforms_test.py -v`
- RED result: the seven new tests reached the missing behavior. Symbolic entries
  were rejected as non-vec4 value layouts or were treated as value arrays;
  matrix/color lowering and count/ambiguity assertions failed as expected.
  Legacy malformed/nonfinite-value rejection and the uniform-free shader case
  already passed.
- GREEN command: `python tests/shader_symbolic_uniforms_test.py -v`
- GREEN result: all 7 tests passed with real shaderc SPIR-V. They cover
  byte-identical symbolic/value transform and pixel-color output, nonsymmetric
  matrix member layout, matrix/tint capacity failures, invalid and ambiguous
  counts, the 256 boundary, legacy malformed values, and a shader without
  uniforms.
- Full suite: `python -m unittest discover -s tests -p '*_test.py' -v` — 77
  tests passed.

## GPU baseline

Generated the pair into the new directory `build/shader-gpu-probe-phase2c`
using `tools/shader-probe/gpu_probe.py`. No XEX was built or launched.

| Artifact | Generated SHA-256 | Brief baseline | Match |
| --- | --- | --- | --- |
| VS `vertex.bin` | `40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF` | `40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF` | yes |
| PS `pixel.bin` | `D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A` | `D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A` | yes |
| Header `probe_shaders.h` | `54280AB38640BCB3AE598535350FCD3348F7C2EEDC3E1A78537EC4822B5EFE5B` | `54280AB38640BCB3AE598535350FCD3348F7C2EEDC3E1A78537EC4822B5EFE5B` | yes |

The GPU upload validator was also exercised with symbolic uniform metadata and
rejected it as malformed because it has no `register_values`; no upload values
are synthesized.

## Changed paths

- `tools/shader-probe/lower_xenos.py`
- `tests/shader_symbolic_uniforms_test.py`
- `tools/shader-probe/README.md`
- `docs/superpowers/plans/x360-shader-symbolic-uniforms-report.md`

No material concerns found within the requested lowerer scope.
