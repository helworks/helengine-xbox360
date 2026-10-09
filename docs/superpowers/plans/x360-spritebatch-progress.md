# SDD ledger — plan: docs/superpowers/plans/2026-09-25-x360-shared-spritebatch-textured.md

User approved design and implementation plan, with Luna execution. Base HEAD:
ac5be3d7e9e2e3b4e37378853e45b70ae19ca9ad. Existing dirty checkout preserved.
No worktrees/commits/emulator or unapproved independent review.
Continue the established docs/plans artifact fallback: .superpowers was previously
inaccessible. No cleanup/deletion of other plans' artifacts.

## Preflight / task state

| Tasks | Shared file/interface | Check |
| --- | --- | --- |
| 1/2 | backend_protocol.py, tool identity | Sequential; name preservation precedes layout field. |
| 1/3 | optimized reflection names | C# consumes unchanged reflection schema; no shared edits. |
| 2/3 | optional vertex_layout/profile IDs | C# emits exact IDs; missing legacy request remains mesh default. |
| 1 | real optimized compilation | RED/GREEN required; no guessed names or disabled optimization. |
| 2 | fixed physical layouts | Explicit profile, no compacted sparse offsets; legacy hashes unchanged. |
| 3 | local compiler options/result/snapshot | Wrong worker profile rejected; no generic shared API change. |

Task1: running — Nash, 01a0d989-8b36-7132-98ba-3b9188099a1a,
gpt-6-luna xhigh (available Luna model). Task2: pending. Task3: pending.

Task1 delivered: optimization-report.md; parent full Python96/96 in12.611s,
exit0. Parent full .NET80/80, exit0, after restoring current execution identity.
Review: Kuhn 01a0d996-85e2-76b1-89b2-5cebc598419e, Luna high, task-only diff
against .artifacts/spritebatch-task1-before, not all prior dirty work.

Task1: fix round1/5 addressed exactarity of OpLine(3)/OpNoLine(0). Parent reran
3 parserarity tests +1 real unsupportedruntime/debugmetadata test, allpassed.
Scoped rereview PASS. Minor caveat: syntheticarity fixtures do not establish
fullSPIR-V placementvalidity; real optimizedshader test separatelycovers valid
compileroutput. No fullISAvalidation claim. Task1: complete (no commits).
Task2 may now start. Task3 remains pending.
Task2 running: Copernicus 01a0d99c-c5e6-7491-b1a3-633ce38e172b, Luna high.
Pretask existing-file snapshots: .artifacts/spritebatch-task2-before.

Task2 initial delivery: parent Python106/106 passed. New realsharedVS required
row_major OpMatrixTimesVector, not previously supported; implementer added4dp4.
Ruling: allow this bounded lowering addition to meet approved actualTexturedVS
acceptance, but require independent numerical binary-execution tests before
review. Compilation alone does not validate transpose/masks. Cost ifwrong:
rework this narrow math path before publishing any artifact/runtime integration.
Ownership extended to test-only execute_xenos oracle in shader_lowering_test.py
(pretask snapshot saved), for dot4 decoding and non-symmetric matrix proof,
including secondvertex/full96bytestride and UV/color pass-through.
Independent oracle reference: local Xenia src/xenia/gpu/ucode.h:1482-1486
documents kDp4=15 and replicated four-component dot product before writemask.
Graph Dot4 search empty, headercoveragepartial/metadata_changed; direct bounded
read confirmed semantics. This supports finite float32 testoracle, not hardware
precision/NaN guarantees.
Parent parallel work: verify actual Batch2D SPIR-V input mappings for Task2,
read-only; do not duplicate Task1 name-preservation implementation.

Parent mapping probe: direct real shaderc VS compilation of the unmodified shared
source, optimize=false, followed by parse_spirv inspection (no layout monkeypatch).
Textured has locations0/1/2 = position/texLocal/color, all float4. RoundedShape
VS has locations0..5 = position/texLocal/color/shape/corners/borderColor, all float4.
These confirm proposed profile offsets0,4,8,12,16,20 dwords and full stride24;
they do not prove RoundedShape PS lowering or GPU execution. First probe command
misused load_shaderc's tuple result and failed before compiling; corrected probe
unpacked library/path and succeeded. No files changed by either probe.
Graph generation2026-09-25T17:07:51Z found reflect_layout/compile_hlsl_from_source;
coverage metadata_changed, so actual functions/parser were read directly.

Parent C# preparation: normal sandbox build failed writing an existing generated
sourcelink file. Escalated no-restore build then exposed stale NuGet asset paths
under CodexSandboxOffline; required xunit analyzer existed in Helena's cache.
Escalated `dotnet restore builder.tests/helengine.x360.builder.tests.csproj
-p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --ignore-failed-sources
--verbosity quiet` passed. Same escalated `dotnet build ... --no-restore --nologo
--verbosity quiet` passed with 0 warnings/0 errors. This is build-only evidence,
not a new 80-test result. No shared source files changed. Task3 tests should use
the same execution identity/permissions as restore, avoiding stale cache roots.

Task2 complete: Meitner scoped review PASS, no actionable findings. Parent fresh
Python108/108 passed in16.981s, exit0, including independent numeric followup.
Actual shared Textured pair in both optimization modes: VS45words/5registers,
PS15words/4registers, control0x00100304, uploads66+18+10=94dwords.
Task3 brief updated with measured costs and binary hashes. No runtime/GPU claim.
Task3 running: Lorentz 01a0d9b2-ec0a-7390-8a55-4b1068ef728b, Luna high.
Pretask snapshots: .artifacts/spritebatch-task3-before. Parent retains verification
and scoped task review; no independent whole-branch review authorized.
Task3 delivered stable: worker focused16/16, full95/95 .NET. Parent independently
full95/95 .NET (exit0) and108/108 Python in13.896s (exit0). Initial report94 was
updated after a final test addition; review diff regenerated byte-identical,
SHA256 EB0448592F5E658FEE429FBC483DC7C8377EC75B2F0E92590C6292D72DB8A657.
Task3 scoped reviewer Mencius 01a0d9c0-d51c-7823-9d21-08b1888e90ed, Terra high.
Task3: complete, spec PASS and quality PASS, no actionable findings, no commits.
Cross-task verification: Task2 reviewed and Python108/108 verified; shared HLSL
hash remains unchanged. All task ownership excluded shared Engine/PS3 writes.
GPU/render behavior intentionally not verified or claimed. All three host tasks
complete; no independent whole-branch review launched (requires user approval).
Next gates remain serializer/C-reader, RoundedShape lowering, cooker/runtime
integration and eventual explicitly authorized emulator validation.
