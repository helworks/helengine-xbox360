# Phase 2a: preserve compiler request inputs

Read this first; this is the bounded implementation task, not all Phase 2.

## Scope and ownership

Own `tools/shader-probe/shader_probe.py`, new `tools/shader-probe/shader_includes.py`
if needed, new `tests/shader_request_frontend_test.py`, and an appended section
of `tools/shader-probe/README.md`. Do not change lowerer, assembler, diagnostic
generator, existing tests, builder, runtime or shared Engine. You are not alone:
preserve concurrent edits. No worktrees, commits, emulator, screenshots or desktop
control. No subagents. Use apply_patch and TDD.

## Requirement

Prepare the existing single shaderc frontend for a future C# shared backend.
Keep `compile_hlsl(library, source_path, stage, entrypoint)` working identically.
Add a reusable in-memory API accepting source bytes, diagnostic source name,
stage, entry point, defines, and a caller-supplied include resolver. Use shaderc's
actual preprocessor, not regex include expansion. The resolver receives requesting
source and requested include name and returns resolved source name + bytes.
Use real shaderc include callbacks with correct native struct layout/lifetime;
contain Python exceptions at callback boundary and convert them into useful compile
diagnostics. Bound include nesting to avoid recursive runaway. No implicit disk
read in this in-memory API; the resolver is the source of included bytes.

Apply defines through shaderc compile options, retaining values and diagnosing
invalid names/NUL bytes rather than truncating silently. Honor arbitrary entry
points. Optional options: debug info, optimize, warnings-as-errors (defaults keep
current output). If an option cannot safely be supported by the lowering route,
reject it explicitly at that boundary rather than ignoring it. This task only
changes the frontend, not lowering capabilities. Successful compilation should
allow caller access to warnings/diagnostics and effective included source identities
without breaking existing bytes-returning compile_hlsl callers; choose a small
result type for the new API. Do not build a cache or another lowering implementation.

## Acceptance tests (real installed DLL)

- Existing full Python suite remains passing.
- In-memory source with a non-main entry point and a define changes real SPIR-V
  when the define value changes; macro names/values are preserved.
- Nested includes resolve using resolved requesting filenames; changed include
  content changes SPIR-V. Inactive #if include never calls resolver.
- Missing include and resolver exception produce useful diagnostics without
  callback traceback/native crash; recursive includes fail in bounded depth.
- Embedded NUL in source/filename/entry/defines/include payload is rejected safely.
- Repeated success/failure compilation does not retain stale resolver state.
- Debug/optimization/warnings option behavior is verified with actual shaderc
  outputs or meaningful diagnostics, not only mocked setter calls.
- Existing transform/textured fixture output stays byte-identical with defaults.

DLL: C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll
Report to `docs/superpowers/plans/x360-shader-request-frontend-report.md` with
RED/GREEN commands/output, changed paths, API contract and limits. Return a concise
DONE/DONE_WITH_CONCERNS/BLOCKED/NEEDS_CONTEXT report. No commit.

## Parent graph evidence

Verify-tier graph search in C-dev-helworks-helengine-x360 found
tools.shader-probe.shader_probe.compile_hlsl at lines62..103. Index generation
2026-09-23T20:18:49Z; exact-path coverage for frontend/tests/xenos_probe/lowerer
reports metadata_changed, no recorded parse gap. Parent read frontend/API and
xenos_probe directly; read remaining relevant source before changes. Current
compile_hlsl initializes/releases compiler/options/result per call, reads a file,
sets HLSL + Vulkan1.0, and returns SPIR-V bytes. No include/define support today.
Shared IShaderIncludeResolver.Resolve(requestingFile, includePath) is the future
adapter contract; no edits to shared files in this task.
