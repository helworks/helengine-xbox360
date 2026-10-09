# Phase 2c: symbolic uniform layout for the single lowerer

Implement only this bounded promotion step. Existing checkout, no worktrees,
commits, subagents, emulator, screenshots or desktop control. You are not alone;
preserve all other changes. Use apply_patch and TDD.

Own `tools/shader-probe/lower_xenos.py`, new
`tests/shader_symbolic_uniforms_test.py`, and an appended README section.
Do not edit assembler, frontend, GPU generator, existing tests, C#, shared Engine
or runtime. Parent investigates C# backend/protocol while you implement this.

## Contract

Keep `lower_spirv(binary, stage, entrypoint, layout)` and every existing legacy
value layout working with identical generated binaries/metadata. Add a symbolic
uniform mapping form: `"uniforms": {"0": {"register_count": 5}}`.
It declares stage-local float4 constant capacity c0..c4, not uploaded values.
Accept integer counts1..256, excluding bool; reject zero, negative, noninteger,
excessive counts and mixed register_count with registers/values. No fabricated
zero arrays or second lowering path. Emit the same Lane("c", index, lane)
references via the existing implementation and validate every referenced register
against declared capacity, including all four matrix registers.

Symbolic uniform metadata must expose `register_count` and existing descriptor
set/binding/raw_memory_order/members, but omit `register_values` entirely. Existing
value-based metadata stays unchanged so the opt-in GPU probe/header hashes stay
identical. Preserve b0/set0-only, member alignment, matrix-major/stride and all
existing unsupported-feature checks. Compilation cannot depend on draw values.

Use a small internal normalization helper/representation if useful; do not
duplicate arithmetic or matrix lowering. Existing probe validation of finite
vec4 upload values stays active for value-based callers. GPU generator is not
allowed to silently synthesize values from a symbolic layout.

## Tests, actual shaderc

- RED new symbolic transform/color test before code change.
- Same real SPIR-V + equivalent symbolic vs value layout produces byte-identical
  assembly/binary; metadata differs only in layout-vs-values representation.
- Nonsymmetric transform fixture references c0..c4 correctly; symbolic metadata
  preserves offset/matrix orientation/stride/member register sets, no values.
- Pixel uniform color works with one symbolic register; no uniform shader works
  without any mapping; insufficient matrix/tint capacity fails usefully.
- Table-test invalid counts and ambiguous layout; existing malformed/nonfinite
  value layouts still fail. Boundary256 allowed,257 rejected; do not claim new
  instruction/hardware capabilities.
- Full Python suite and regenerate GPU pair into a NEW build output directory,
  comparing VS/PS/header hashes to baseline below. No XEX rebuild/launch.

DLL C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll.
VS hash40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF;
PS D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A;
header54280AB38640BCB3AE598535350FCD3348F7C2EEDC3E1A78537EC4822B5EFE5B.

## Graph/source evidence

Verify search project C-dev-helworks-helengine-x360 generation2026-09-23T22:34:34Z:
lower_xenos._Lowerer._uniform_value587..627, _record_uniform_member629..643,
_constant_vector645..653, lower_spirv996..1005. Coverage metadata_changed on
lowerer/gpu_probe/existing lowering tests/assembler; parent read exact uniform
code and fixture. _constant_vector validates values but returns symbolic lanes;
_record_uniform_member currently always writes register_values. GPU generator
validates <=256 stage constants and requires real values for upload. Read the
remaining relevant source directly before changes; no broader graph crawl needed.

Report to docs/superpowers/plans/x360-shader-symbolic-uniforms-report.md with
RED/GREEN commands/results, changed paths and concerns. Return short status.
