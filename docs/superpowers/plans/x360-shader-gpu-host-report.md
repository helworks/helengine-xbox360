# Bounded X360 GPU probe host generator report

## Result

Added an opt-in host generator that compiles the authored vertex/pixel HLSL through `shader_probe` and `lower_spirv`, validates the pair against the fixed runtime vertex layout and tf1 texture pair, then emits a deterministic C header, manifest, and per-stage `.bin`, `.xasm`, and `.json` evidence. The header is a generated bridge for the diagnostic worker, not a final artifact ABI.

The VS interface width and location checks use the compiled SPIR-V declarations. The generator checks float32 widths, position and color0, paired varying locations and physical interpolator slots, exact vf0 inputs, texture-fetch aliases, uniform metadata, finite constants, hardware register fields, and the 1024-dword limit. Output alias checks run before cleanup; failures clear only the generator's named outputs.

## Upload cost

For the current fixtures, `HEL_PROBE_UPLOAD_DWORDS` is **100**:

| Component | Dwords |
| --- | ---: |
| VS + PS microcode (51 + 21) | 72 |
| PM4 immediate-load packet overhead (3 per stage) | 6 |
| VS constant payload (5 vec4) | 20 |
| VS constant packet header | 2 |
| Total | 100 |

The macro and manifest exclude the 10 SQ state-write dwords. The manifest states this explicitly so the runtime can add those when comparing ring headroom. Program binary arrays decode from the serialized big-endian words; constant arrays contain explicit IEEE-754 float32 bit patterns. Empty constant arrays use a zero sentinel with count macro zero.

## TDD and verification

- RED: the new test module failed to import `gpu_probe` before implementation (`ModuleNotFoundError`), as expected.
- RED/GREEN follow-up: a finite VS constant change first failed against an overly strict fixture-value equality check, then passed after that check was removed; the emitted constant words and probe identity both change.
- GREEN: `python tests/shader_gpu_probe_test.py` passed all 9 focused tests against the installed shaderc DLL and real HLSL fixtures.
- Final verification: `python -m unittest discover -s tests -p '*_test.py'` passed all 60 tests.

## Changed files

- `tools/shader-probe/gpu_probe.py`
- `tests/shader_gpu_probe_test.py`
- `tests/shaders/fixtures/gpu_probe_vs_layout.json`
- This report.

## Limits

The generator supports only the brief's diagnostic stages and authored `main` entrypoints. Vertex input layout remains fixed, while finite uniform values may vary within the validated register limits. It does not validate GPU execution or replace the runtime's ring overflow guard. The current metric excludes SQ state writes as described above; the runtime worker should include those separately in its ring comparison. The output directory must be separate from the parent directories containing protected source, layout, tool, and compiler files.
