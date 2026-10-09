# Lowering follow-up: resolve the observed matrix, finish a verifiable narrow slice

Continue Task 2 in its existing owned files. No renderer/shared changes. The parent, not the user, requested the checkpoint. Correct that attribution in your report. Preserve others' edits, no agents/worktrees/commits/UI.

## Concrete matrix semantics (parent ruling)

SPIR-V logical matrices have columns. RowMajor/ColMajor describe memory, not a change to mathematical multiplication. Retain the member decorations through Pointer -> uniform load; require float4x4, MatrixStride=16 and an explicit exactly-one RowMajor/ColMajor decoration. Reject missing/unsupported layout rather than guessing.

The explicit layout's `uniforms["0"]` is RAW MEMORY ORDER: consecutive vec4 chunks to upload, not a list named mathematical rows/columns. With RowMajor those chunks are rows of the SPIR-V logical matrix. For `OpVectorTimesMatrix` 144 the result is the sum of each MEMORY ROW scaled by the corresponding input scalar. With ColMajor, the same weighted-vector implementation is valid for `OpMatrixTimesVector` 145. You may support only these two combinations for now; explicitly reject the complementary combinations until dot-product assembly is implemented/tested.

For the HLSL fixture `column_major M; mul(M, position)` shaderc emits RowMajor logical SPIR-V matrix and OpVectorTimesMatrix. To represent this HLSL mathematical matrix:

```
2   3   5   7
11 13  17  19
23 29  31  37
41 43  47  53
```

upload consecutive c registers as HLSL COLUMNS (also SPIR-V memory rows here):

```
c0 = [2,11,23,41]
c1 = [3,13,29,43]
c2 = [5,17,31,47]
c3 = [7,19,37,53]
c4 = tint
```

Input position [1,2,3], composed homogeneous [1,2,3,1], must export [30,107,211,321]. The existing weighted sum implementation can be reused after naming/dispatch/layout checks are correct; do not physically transpose these upload chunks a second time. Test against these literal values using an independent test-only microcode decoder/executor.

## Finish bounded scope

1. Fix opcode table based on Khronos grammar, and prove actual shaderc FAdd/FSub inputs numerically. FDiv, texture sampling, control flow remain explicit errors for this slice. No broad ISA implementation needed.
2. Numerical tests replace brittle expectations like exact `mad oPos`/unqualified `c0` strings: assembling is not a semantic test. Decode emitted binary instructions using published Xenos fields, not lowerer helper state. Verify transform, uniform PS, interpolated-color multiply/add/sub; changed expression changes output numerically. Keep test decoder narrow and fail on unknown instructions.
3. Enforce safe declared subset: float32 only; sole uniform buffer b0/set0 for now (reject others instead of aliasing all buffers to c0); explicit varying map, unique and bounded slots; no silently ignored semantic decorations/capabilities. Reject malformed operand counts with LoweringError/SpirvError rather than IndexError/KeyError. Preserve output stage validation and actual register exhaustion test that reaches allocator.
4. Include actual uploaded constant VALUES and offsets/order in metadata, not merely indexes. Include input/varying maps, register count vs highest/SQ field, explicit unsupported subset and .bin byte order. Preserve raw output conventions; not a final runtime format.
5. Build xenos_probe.py CLI for HLSL->SPIR-V->lowering OR precompiled .spv, explicit layout JSON and output prefix. Validate output paths don't alias source/layout/library or one another; invalidate only explicit outputs on failure, including tool setup failure. Both paths need input/source/SPIRV hashes; HLSL also native DLL hash. Use existing frontend without API changes.
6. Provide checked-in layout JSON matching physical 12-dword runtime vertex format: position dword0, UV dword6, color dword8, stride12. Multiple vertex fetches may use vf0 referencing same buffer. Explicitly document whether offset is dwords (assembler convention), not bytes.
7. Remove task-owned orphan tests/shader_probe_test.py via apply_patch ONLY after corresponding positive/negative cases are represented in frontend/lowering suites. Its abandoned API must not remain discovered/skipped misleadingly. Parent authorizes this exact removal. No user files removed.
8. Run complete Python discovery: `python -m unittest discover -s tests -p '*_test.py' -v`. Report actual supported scope and remaining texture/runtime/GPU gates. If another blocker appears, provide exact minimal failing fixture and proposed fix early.

Prefer a correct transform+color slice over pretending all of Gate A is complete. Texture remains deferred and must reject cleanly. Write final usage README in tools/shader-probe and update lowering report with exact RED/GREEN commands/results. Do not enlarge the current source file indefinitely: split resource mapping or emitter into a focused module if required by this concrete work, not a general framework.
