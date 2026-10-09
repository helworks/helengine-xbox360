# Task 2 — Bounded SPIR-V to Xenos lowering

This is the second slice of Phase 1, not final backend integration. User requested Luna implementation. Existing checkout C:/dev/helworks/helengine-x360, no worktrees or commits. No UI, screenshots, emulator launches or extra agents. You are not alone; preserve others' edits.

## Files owned

Create focused modules beneath tools/shader-probe/ for SPIR-V parsing and Xenos lowering, plus xenos_probe.py CLI, tests/shader_lowering_test.py and tests/shaders/fixtures/. Existing shader_probe.py is frontend-owned: consume its compile_hlsl/load_shaderc API, request changes from parent rather than edit concurrently. Leave runtime/builder/shared repos untouched. Avoid assembler changes unless verified encoding demands them; ask parent first if needed.

## Inputs available

tools/shader-probe/shader_probe.py implements load_shaderc(path) -> (library,path), compile_hlsl(library, Path, stage ('vertex'/'pixel'), entrypoint) -> SPIR-V bytes. It uses real shaderc. Native DLL available at C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll; CLI/environment must allow alternate path. Its current fixes only affect Vulkan env version, identity and safe outputs, not this API. Existing tools/xenos_asm.py assembles exec/exece/alloc, vfetch full/mini, tfetch, add/mul/max/min/mad/dp3/dp4. Read actual shaders/unlit.vs.xasm and textured.ps.xasm for conventions. Vertex input r0.x is vertex index; pixel input interpolator N is rN; texture slot 0 aliases vertex fetch, use explicit maps avoiding collision.

## Deliver a real narrow compiler

Implement SPIR-V parser (header, instruction size/bounds, entrypoint/stage, types/constants/decorations/functions) and lowering for straight-line float scalar/vector expressions and a matrix position transform. Actual emitted instructions must depend on authored source. No canned shader selection. Unknown operations, control flow, dynamic indexing, unsupported resources/decorations/types fail descriptively. Never silently drop semantic instructions. Do not claim support for arbitrary SPIR-V.

Start with actual shaderc output from fixtures, inventory its opcodes, then implement necessary subset. If optimization is needed to eliminate frontend scaffolding, request a documented frontend option from parent instead of pattern-matching HLSL. Keep primitive compiler complexity bounded; explain precise supported subset.

Minimum acceptance examples: VS transformed position (non-symmetric constant or uniform matrix), vertex color/UV passthrough, PS uniform or literal color, PS multiplication of interpolated color. Add Texture2D sample if feasible using existing verified tfetch; otherwise explicitly report it unsupported rather than fake it.

Use explicit configurable vertex input mapping (SPIR-V location -> physical stride/offset/format) and deterministic varying map. Do not guess semantics by variable ordering. You may restrict first fixture ABI to supplied map and report it in metadata. Generated immediate/uniform constants require an explicit upload map/value table; binary alone is not a runnable shader.

Correct register allocation and dependencies: avoid clobbering vertex ID or PS interpolators; enforce actual r0..r63 destination encoding bound and constant limits. A conservative allocator is acceptable for this probe if it rejects exhaustion; document whether it reuses liveness. Scheduling must be safe using existing serialize/exec conventions, not arbitrary instruction grouping. Exports and alloc must be valid for stage. Record highest register index vs encoded hardware count clearly.

CLI consumes either authored HLSL via existing frontend or .spv plus explicit stage/entry and layout config, produces .xasm, .bin and provisional metadata JSON. Include native compiler hash for HLSL route, source/SPIR-V hash, constants/resource/input/output maps. No valid success outputs on failure, paths must not alias inputs/tools. Do not claim runtime integration.

## Tests and evidence

Write failing tests before implementing. Test actual HLSL->SPIR-V->Xenos, source expression change, non-symmetric transform numeric expected output, malformed instruction bounds, unsupported opcode/stage and register exhaustion. Numerical expected results must be hand-derived; a small test-only executor may decode emitted microcode, but must not call lowering helpers to reproduce expected output. Compare simple emitted operations to independently inspected assembler fixtures and keep 13 existing assembler tests green. Prove source change affects computed color, not just hash.

Run focused tests and entire available Python suite at the end. Host C suite currently cannot run (gcc absent). Report CPU validation separately from Xenia/GPU validation. GPU Gate A remains open until actual runtime evidence.

## Discovery notes

Use codebase-memory-mcp for structure queries. Parent queried C-dev-helworks-helengine-x360 and C-dev-helworks-reference-xenia-canary; metadata_changed flagged relevant sources, ucode.h partial parse. Read actual source ranges for ISA details, not graph absence. Xenia shader translator direction is Xenos->host, not a compiler we can call backwards.

## Report

Write docs/superpowers/plans/x360-shader-lowering-report.md with files, exact test commands/RED-GREEN output, supported subset and missing coverage. Return short DONE/DONE_WITH_CONCERNS/BLOCKED and concrete concerns. If blocked on a specific opcode/scaffolding, report quickly with an actual fixture/opcode list so parent can decide; don't spend many minutes on broad unrelated exploration.
