# Phase 1 follow-up: bounded 2D texture lowering

Implement the next CPU compiler slice, not runtime integration. Read this first; this is the task's requirements.

## Scope / ownership

Own tools/shader-probe/lower_xenos.py, spirv_parser.py (only necessary parsing), README.md, tests/shader_lowering_test.py and tests/shaders/fixtures texture-specific files. Reuse tools/xenos_asm.py encode_tfetch unchanged if sufficient; request before modifying assembler. No frontend/builder/runtime/shared Engine/PS3 edits. You are not alone: preserve all concurrent edits. No worktrees, commits, emulator, CUA, captures, or subagents. Use apply_patch.

## Implementation

Compile the real texture_ps.hlsl through installed shaderc, inspect the resulting instructions. Support pixel-stage Texture2D<float4> plus separate SamplerState, normalized float2 coordinates, plain implicit-LOD sampling (OpLoad resource handles -> OpSampledImage -> OpImageSampleImplicitLod), followed by authored float arithmetic. Reuse existing tfetch2d assembler and conservative registers. Preserve ALU-coordinate/fetch/dependent-ALU order and CF serialize dependency bits; inspect local assembler and Xenia decoder instead of guessing scheduling.

Use explicit layout texture/sampler pair mappings to physical fetch constants 0..31; logical bindings are not physical slots. Resource kinds disambiguate texture t0 and sampler s0 even if shaderc gives both binding0/set0. A proposed `texture_bindings` list has entries `texture_set`, `texture_binding`, `sampler_set`, `sampler_binding`, `fetch_constant`. Validate exact integer/range/duplicate constraints, require each sampled pair to resolve uniquely, reject physical slot collisions between distinct pairs. Metadata records the actual logical pair, physical slot, stage and dimensionality. No implicit fallback slot0. Sampling state remains a runtime fetch-constant contract, not baked from a guessed sampler.

Reject non-pixel sampling, non-2D, arrays, multisample, depth/comparison/integer/storage images, optional sampling operands (bias/offset/etc), resource arrays, unsupported resource forms and malformed operand lengths explicitly. Keep existing negative tests meaningful: texture without a mapping should fail, not silently become supported.

## Tests / acceptance

TDD: focused failures first, record RED/GREEN. Real shaderc fixture succeeds with explicit mapping, emits decoded texture fetch fields (source, swizzle, destination, dimension, fetch slot, normalized coords, computed LOD) and correct serialized CF dependency bits. Extend test-only decoder/executor to sample a deterministic small RGBA texture at known texel centers using a declared point/clamp/no-mip CPU model. Execute emitted microcode and check UV routing, RGB and alpha modulation; use nonzero physical slot and source expression/UV changes to rule out canned shaders. Test arithmetic before sample plus sample-dependent arithmetic after it. Test missing/duplicate/colliding/out-of-range mappings, wrong resource types and unsupported sample operands; CLI must leave no partial/stale artifacts on failures. Existing tests remain green.

CPU sampling is an algebra/routing oracle, NOT derivative/filter/interpolation/device validation. Document this explicitly. Gate A remains open pending GPU execution; don't enable shared backend on CPU evidence alone.

Run focused tests during work, then python -m unittest discover -s tests -p '*_test.py' once final. Report full changed file list, RED/GREEN commands/output, accepted subset and remaining limits to docs/superpowers/plans/x360-shader-texture-report.md. Return short status and concerns. No commits.

## Evidence available

Graph project C-dev-helworks-helengine-x360 generation 2026-09-23T16:01:42Z, recording complete; coverage for lower_xenos/parser/tests/assembler says metadata_changed, so read current source. Exact graph symbol tools.xenos_asm.encode_tfetch (162..205), parent read source: normalized coords, filters from fetch constant, use_comp_lod=1, dimension=1, slots<32. Actual reference assembly is shaders/textured.ps.xasm (not texture.ps.xasm). Bounded graph search lower_xenos returned 10/44 symbols; no exhaustive claim. Parent read lowerer initialization/module validation and parser tables; remaining code needs direct reading. Local Xenia reference: C:/dev/helworks/reference/xenia-canary/src/xenia/gpu/ucode.h. Shaderc DLL: C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll. Baseline Python suite46 passed in preceding milestone.
