# Task1 — optimized reflection names

Read this first: bounded task in the approved shared SpriteBatch Textured plan.
Work in C:/dev/helworks/helengine-x360 only, existing checkout, no worktrees,
commits, subagents, emulator or UI. You are not alone: preserve all prior work.
Do not implement layout selection/Task2 or C# integration/Task3.

Ownership: tools/shader-probe/shader_probe.py, backend_protocol.py; only if
needed metadata handling in spirv_parser.py/lower_xenos.py; tests in
tests/shader_frontend_test.py and tests/shader_backend_protocol_test.py;
docs/x360-shader-host-protocol.md. No shared Engine/PS3 writes.

Goal: real optimized shaderc compilation preserves existing resource/member
reflection names without inventing names, disabling requested optimization,
or joining unrelated optimized/unoptimized IDs. Keep compile_hlsl_from_source
and compile_request request semantics; document effective internal policy.
Tool identity already hashes actual Python tool bytes and shaderc DLL.

Use TDD and systematic diagnosis. First add failing realcompiler test that reads
the EXISTING shared source (do not copy HLSL):
C:/dev/helworks/helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl.
Textured PS entrypoint PS, variant Textured, no rounded define. Request
options optimize=true, debug_info=false, warnings_as_errors=false. Require real
success and reflection names BatchTexture/BatchSampler. Parent reproduced
failure: resource variable %17 has no OpName. Textured VS optimized similarly
loses BatchCameraBuffer/struct names, but its Batch2D input layout is a later task.
Source hash observed 3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2;
recheck to identify concurrent changes. Existing tests supply shaderc path/harness.

Run focused RED, compare real optimized output with shaderc metadata/debug
generation enabled, and implement explicit internal name preservation with
performance optimization STILL enabled. Internal reflection metadata is distinct
from the caller's request for debug artifacts. If this approach cannot retain
names, report concrete blocker rather than guessing a mapping.

Test matrix member names, optimized includes, debug_info both false/true,
and explicit rejection where required names cannot be established. Ensure no
new unsupported runtime instructions are accepted just to ignore debug metadata.
Run full Python suite once at end:
python -m unittest discover -s tests -p '*_test.py'
Legacy unoptimized microcode hashes must remain:
VS40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF
PSD70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A.
No C# suite needed for this Python-only task; parent will handle integration.

Write report docs/superpowers/plans/x360-spritebatch-optimization-report.md:
exact RED/GREEN commands/counts, files, effectivepolicy, evidence optimization
still enabled, sourcehash, limitations. Return concise DONE/NEEDS_CONTEXT/BLOCKED.
Do not keep preflighting when ready: write and run first regression before
implementation. No further user approval is needed for this approved task.
