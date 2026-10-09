# Task2 — explicit Python vertex layout profiles

Read this first. Execute only after Task1 optimized reflection is accepted.
Same checkout, no commits/worktrees/subagents/emulator/UI/shared Engine or PS3
edits. Preserve existing dirty files. TDD/apply_patch. Task1 is a prerequisite,
not work to redo. Do not edit C# (Task3).

Ownership: new tools/shader-probe/vertex_layout.py; backend_protocol.py,
backend_layout.py; lower_xenos.py only as needed to consume explicit layout;
new tests/shader_vertex_layout_test.py; focused existing Python tests where
tool-bundle enumeration must include the new module; protocol documentation.

Introduce optional protocol1 request vertex_layout with exact string values
mesh3d-v1 and batch2d-v1. Omitted normalizes to mesh3d-v1; null/bool/number/
unknown values fail before shaderc invocation. Explicit mesh and omitted mesh
normalize equivalently. Include normalized profile in compile identity for
BOTH stages, including when PS code is unchanged. Successful metadata reports
vertex_layout for both stages. Existing diagnostics/protocol limits unchanged.

reflect_layout(binary, stage, entrypoint, vertex_layout='mesh3d-v1') accepts
explicit selection. Do not infer profile from source/program/resource names or
active input subset. Keep generic callers defaulting to mesh. Profiles:
- mesh3d-v1: vf0 stride12, loc0 float3/off0/32_32_32_FLOAT,
  loc1 float3/off3/sameformat, loc2 float2/off6/32_32_FLOAT,
  loc3 float4/off8/32_32_32_32_FLOAT. Same legacy microcode hashes.
- batch2d-v1: vf0 stride24, loc0..5 all float4, offsets0,4,8,12,16,20,
  hardware format32_32_32_32_FLOAT. Full stride even with sparse inputs.
PS varying rules unchanged. tf1 remains disjoint from vf0.

Parent realshaderc SPIR-V observation on unmodified shared source, opt=false:
Textured VS loc0=input.position,1=input.texLocal,2=input.color, allfloat4.
RoundedShape VS loc0..5 adds shape/corners/borderColor, allfloat4. Thus Textured
active offsets0/4/8, not mesh offsets. Source is read-only shared file:
C:/dev/helworks/helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl.
Do not copy it. Read exact variants from agreed names/defines for Python tests;
C# later consumes actual shared SpriteBatchShaderVariants.All.

TDD: RED shared Textured VS+explicitbatch must compile and expose correctfetches,
same source/defaultmesh must reject float4 input. Test invalid profiles before
compiler invocation, exact unchanged meshfixture output, sparse physical offsets,
wrongwidth/outofrangelocations, both optimized/unoptimized shared Textured VS,
profile compile identity for identical PS and normalized omitted/explicitmesh.
All required resources remain reflected, optimized policy inherited fromTask1.
Assert legacy hashes:
VS40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF
PSD70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A.

Include vertex_layout.py in actual tool-source identity and Python copiedbundle
tests; C# csprojcopy is Task3 so no C# test/build here. Run focused then full
python -m unittest discover -s tests -p '*_test.py'. Update docs with compatibility
and request/resultfields. No RoundedShape PS lowering or GPU execution claims.

Write docs/superpowers/plans/x360-spritebatch-layout-report.md with RED/GREEN,
commands/counts, profileinterfaces/schema, files, limits. Return concise status.
