# Task 2 — explicit Python vertex layout profiles

## Result

Protocol 1 now accepts optional `vertex_layout` values `mesh3d-v1` and
`batch2d-v1`. Omitted selection normalizes to `mesh3d-v1`; null, non-string,
and unknown selections fail request validation before shaderc is loaded. The
normalized profile is part of the request and compile identity for both shader
stages, and successful metadata reports it for both stages.

`reflect_layout(binary, stage, entrypoint, vertex_layout='mesh3d-v1')` uses only
the explicit profile. It does not infer layouts from source names, program
labels, or active inputs. Both layouts use `vf0`; pixel texture/sampler fetches
remain on `tf1`.

| Profile | Stride | Location mapping |
| --- | ---: | --- |
| `mesh3d-v1` | 12 | 0: float3/off0/`32_32_32_FLOAT`; 1: float3/off3/same; 2: float2/off6/`32_32_FLOAT`; 3: float4/off8/`32_32_32_32_FLOAT` |
| `batch2d-v1` | 24 | 0..5: float4 at offsets 0, 4, 8, 12, 16, 20; all `32_32_32_32_FLOAT` |

Sparse active fields keep their physical offsets and full selected stride.
The shared unmodified Textured VS compiles under `batch2d-v1` with optimization
both disabled and enabled. Reflection reports locations 0, 1, and 2 as float4
with offsets 0, 4, and 8. The same shared VS under the default mesh profile
rejects its float4 location 0 input.

## RED/GREEN evidence

Initial RED:

```text
python -m unittest discover -s tests -p 'shader_vertex_layout_test.py'
Ran 7 tests — FAILED (6 failures, 1 error)
```

The failures showed that the protocol rejected the new request field, the
selected profile was absent from metadata, and the explicit Textured batch
compile could not pass its physical layout to reflection/lowering.

Focused GREEN:

```text
python -m unittest discover -s tests -p 'shader_vertex_layout_test.py'
Ran 7 tests — OK

python -m unittest discover -s tests -p 'shader_backend_protocol_test.py'
Ran 22 tests — OK
```

Full Python suite:

```text
python -m unittest discover -s tests -p '*_test.py'
Ran 106 tests — OK
```

The existing mesh vertex and texture pixel fixture hashes remain
`40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF` and
`D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A`.
Tests also confirm omitted and explicit mesh selections have the same pixel
compile identity, while batch selection changes identity without changing
the emitted pixel binary.

## Numerical proof for row-major `MatrixTimesVector`

The shared Textured VS numerical test compiles the unmodified source through
real shaderc with optimization disabled and enabled, reflects the explicit
batch profile, lowers the resulting SPIR-V, and executes the emitted binary in
the existing test-only Xenos interpreter. It confirms the SPIR-V matrix member
is `row_major`, and the selected `vf0` inputs are locations 0/1/2 at offsets
0/4/8 with stride 24 dwords (96 bytes per vertex).

The test uploads the nonsymmetric matrix rows `[2,3,5,7]`, `[11,13,17,19]`,
`[23,29,31,37]`, and `[41,43,47,53]`. For position `[1,2,3,1]`, the hand-computed
row dot products are `[30,107,211,321]`; for `[-2,0.5,4,1]`, they are
`[24.5,71.5,129.5,180.5]`. Both vertices also verify UV and color pass-through.
The reflected layout remains symbolic with `register_count: 4`, which the test
asserts; concrete matrix values are passed only to `execute_xenos`. Expected
values are literal test expectations, not calculated by the lowerer or its
instruction-emission helpers.

Oracle attribution: Xenia Canary
`src/xenia/gpu/ucode.h:1482-1486` documents `kDp4 = 15`, four pairwise
component products summed into the destination, and destination write-mask
behavior. The test-only decoder models this with finite float32 arithmetic;
it makes no bit-exact hardware precision claim.

Numerical RED:

```text
python -m unittest discover -s tests -p 'shader_lowering_test.py' -k shared_textured_row_major_matrix_transform_matches_hand_calculation
Ran 1 test — FAILED (4 subtest failures: unknown ALU opcode 15 for both optimization modes and both vertices)
```

Numerical GREEN and affected test modules:

```text
python -m unittest discover -s tests -p 'shader_lowering_test.py' -k shared_textured_row_major_matrix_transform_matches_hand_calculation
Ran 1 test — OK

python -m unittest discover -s tests -p 'shader_lowering_test.py' -k test_test_executor_dp4_sums_four_components_and_preserves_masked_lanes
Ran 1 test — OK

python -m unittest discover -s tests -p 'shader_lowering_test.py'
Ran 27 tests — OK

python -m unittest discover -s tests -p 'shader_vertex_layout_test.py'
Ran 7 tests — OK

python -m unittest discover -s tests -p 'shader_backend_protocol_test.py'
Ran 22 tests — OK
```

## Implementation and files

- Added immutable versioned profile definitions in
  `tools/shader-probe/vertex_layout.py`.
- Updated `backend_protocol.py` for optional-field normalization, profile
  validation, profile-aware reflection, success metadata, ABI identity, and
  tool-source hashing of the new module.
- Updated `backend_layout.py` to validate active input widths and locations
  against the selected profile while retaining the profile’s full stride.
- Updated `lower_xenos.py` to lower the row-major `OpMatrixTimesVector`
  emitted by the actual shared Textured VS as four `dp4` result lanes. The
  SPIR-V carries row-major decoration on `BatchCameraBuffer.projection`; this
  previously stopped the required real-shader compile before layout metadata
  could be produced.
- Added real-shaderc profile tests in `tests/shader_vertex_layout_test.py` and
  extended the Python copied-bundle source-path assertion in
  `tests/shader_backend_protocol_test.py`.
- Extended `tests/shader_lowering_test.py` with the real shared-shader numeric
  proof and a narrow test-only opcode-15 `dp4` decoder plus a destination-mask
  oracle test.
- Updated `docs/x360-shader-host-protocol.md` with the optional request field,
  result metadata, compatibility behavior, and both physical schemas.

## Limits

The shared shader file was read from its agreed location and was not copied or
modified. No C# files, builder copy list, shared Engine/PS3 files, emulator, or
GPU execution were touched. The test interpreter provides a finite-float32
algebraic check, not hardware precision or GPU execution evidence. RoundedShape
pixel lowering and GPU behavior are not claimed.
