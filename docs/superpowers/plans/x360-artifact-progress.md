# SDD ledger — plan: docs/superpowers/plans/2026-09-25-x360-shader-artifact.md

User approved written spec, wire appendix and plan; Luna implementation.
Existing checkout/main, HEAD ac5be3d7e9e2e3b4e37378853e45b70ae19ca9ad.
No worktrees/commits/emulator/shared-source edits. Preserve all preexisting dirty
files. Continue established docs/plans artifact fallback because .superpowers
was inaccessible in this environment; do not clean another plan's artifacts.

## Preflight conflict checks

| Task/interface | Check |
| --- | --- |
| 1 writer/golden | Independent hex fixture, BE code digest versus LE words; tests do not regenerate expectations with writer. |
| 2 envelope/SHA | Bounds before reads; private envelope success is not semantic success. |
| 3 semantics/accessors | Public view published only after full validation; C ABI headers move once without duplicate definitions. |
| 4 exporter/interop | Expected manifest from snapshots rather than re-read serialized file; no shared-source edits. |
| 1/2 fixture/schema | Same 668-byte golden, fixed directory and stage digest algorithm. |
| 1/3 record schema | Exact appendix governs both; no PS/VS profile-equality requirement. |
| 1/4 writer API | Explicit asset string and immutable pair; no source-path fake identity. |
| 2/3 private header/harness | Sequential ownership; 3 adds semantics on existing envelope. |
| 2/4 harness | Existing pinned offline Docker image; source readonly/output separate. |
| 3/4 accessor/harness | Same decoded spans, pair costs and error classes; sequential changes. |

Task1 pending. Task2 pending. Task3 pending. Task4 pending.
Parent fresh baseline:95/95.NET,108/108Python in12.750s, both exit0.
Task1 running: Feynman 01a0da02-56ca-7bf0-a389-90026b46b5fa, gpt-6-luna high.
Task1 owned paths did not exist at preflight; no existing source edit authorized.
Parent parallel work: read-only C dependency and toolchain preflight forTask2.
Preflight complete: elevated offline pinned Docker has hostgcc12.2 and
/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc15.2; defaultPATH lacks PPC dir,
matching explicit export in existing tools/build.ps1. libasan/libubsan resolve.
No runtime sanitizer proof yet. Graph SHA256 src search empty, scope gaps fully
paged; bounded src/platform/xenia_guest +src/engine text search found no helper.
Task2 brief carries these findings. Initial normal Docker call permission-denied;
initial crosscompiler lookup lacked PATH; corrected absolute lookup passed.
Task1 status checkpoint: extended read-only preparation produced no files. Parent
interrupted for concrete status; worker confirmed same cwd, no edits/blocker or
pending tool, and computed independent fixture hashes. Resumed same worker with
direction to write IN PROGRESS report and first failing test immediately, no
further broad exploration. No task redispatched or existing changes reverted.
Task1 stable delivery: worker7/7focused and102/102full.NET. Parent independently
full102/102.NET passed exit0; materializer --verify passed for668-byte golden.
Reviewer Kant 01a0da1e-bba1-7412-9880-2d2380bdcf7b, Terra high, task-only diff
docs/superpowers/plans/x360-artifact-task1-review.diff. Review pending.
Task1 review NOT APPROVED: PS used declaration reference omits stage declaration
base (critical); test misses PS reference; bounds/schema negative coverage thin.
Fix round1/5 resumed Feynman with these findings. Before snapshots at
.artifacts/x360-artifact-task1-fix1-before. Task2 remains pending until rereview.
Task1 fix1: RED PSoffset expected996 actual924; fix includes stage declaration
base. Worker12/12focused,107/107full.NET; parent12/12focused passed. Scoped
rereview: all3findings ADDRESSED, no new breakage. Task1: complete, no commits.
Nonblocking review note: negative additions test table/count bounds, not every
per-record semantic field. Reader semantic mutation coverage remains Task3.
Task2 running: Einstein 01a0da26-cfd6-7903-af96-fe2e6322761e, Luna xhigh.
All Task2 source/harness paths absent before dispatch. Task1 files read-only.
Golden complete-file SHA256698ABA97A517880A854B93378473843285F108851E72C1A3D1BA5D3AC09071C6;
embedded virtualzero digest955313d63bf7a11b633805695ad5fcd19c5ca21b4f7d139fa6b4548be25d0cdd.
Task2 in progress: intermediate host+ASAN+UBSAN+PPC run passed. Final boundary
regression rerun hostpassed but sanitizer availability probe hung in container
p9_client_rpc while executing from output mount; worker stopped only its test
container (exit137). Harness moves sanitized executable execution to container
/tmp, keeps logs/PPC outputs in checkout; final adapted rerun pending. Earlier
PowerShell multiline native-argument issue resolved via base64 script transport.
Task2 delivered stable. Parent fresh harness Envelope exit0 at
.artifacts/shader-artifact/20260925T203342855Z: host C suite, ASAN, UBSAN allpassed;
both freestanding PPC objects compiled. Final harness runs sanitizer executables
in /tmp, uses non-PIE ASAN after default-PIE DEADLYSIGNAL without diagnostic;
probes10s/tests30s bounded. Previous failed attempts preserved in task2report.
Task2 reviewer Kierkegaard 01a0da46-06a1-79d3-a022-c567afe29dde, Terra xhigh.
Task2: complete, specPASS/qualityPASS, no code findings. Reviewer did not rerun
tests; parent directly observed final host+ASAN+UBSAN+PPC success above. Reviewer
notes sanitizer test logs are not retained (only probe logs); Task3 harness
extension will persist actual test logs under existing plan logging requirement.
Earlier failure narrative stays in report; no inference of GPU/ISA safety.
Task3 running: Godel 01a0da4a-3c2d-7091-b6f1-57757c45cf9d, Luna xhigh.
Pretask snapshots of existing internalheader/harness:
.artifacts/x360-artifact-task3-before. New parser/publicheader/semantics/test
paths absent before dispatch. Parent parallel work: Task4 exporter preflight.
Task4 preflight: actual shared catalog/source unchanged; existing test-only rich
fixture helper covers bothstage constants/unused matrices/shifted resources/
Unicode/mixedprofile provenance. Task4 brief points to reusable helper; real
exported shader fixtures must stay separate from these synthetic contracts.
Parent full.NET recheck after Task1 fix passed107/107,exit0 while Task3 C work
was in progress. Task3 interim fullC/ASAN/UBSAN/PPC passed; expanded semantic
mutation tests still running. Initial executable preimplementation semantic
RED was not captured (hostgcc/permissions failures do not count); worker report
states this process limitation honestly. Later genuine REDs caught unknown
fetch-format and matrix-major enums returning SEMANTIC instead of UNSUPPORTED;
fixes and final validation pending. Do not claim initial TDD proof retroactively.
Task3 stable handoff received. Worker final All run exit0 at
.artifacts/shader-artifact/20260925T211737162Z. Parent independently repeated
All: exit0 at .artifacts/shader-artifact/20260925T212042282Z, envelope and
semantic suites passed host/ASAN/UBSAN; all four PPC objects compiled.
Task-only reviewer Hume 01a0da71-0a14-7483-ad2c-54e0bb45c599, Terra xhigh,
reviewing task3-review.diff; Task4 awaits review gate.
Task3: complete, scoped specPASS/qualityAPPROVED, no remaining findings.
Reviewer initially missed inherited Task2 stage hash validation; parent cited
envelope.c72-124/244-247, parser.c37 and existing envelope tests228-248.
Reviewer verified bounded dependency and withdrew finding; no duplicate hashing
or unnecessary source changes. Both Task3 agents closed after stable handoff.
Task4 running: Schrodinger 01a0da75-c634-7660-9c83-6d686cf5a709, Luna high.
Existing C test/harness/testsupport snapshots .artifacts/x360-artifact-task4-before;
four named new Task4 paths absent. Parent owns final bookkeeping only.
Parent fresh Python suite during Task4: python -m unittest discover -s tests
-p '*_test.py', exit0, 108 tests in20.177s. Task4 has no Python source ownership.
Task4 real integration RED found Task3 reader rejecting valid shifted cbuffer
logical slot17. Wire85 fixes logical set0/physical set0/binding0, not logical
slot. Parent confirmed semantics737 extra slot!=0 clause; authorized narrowly
scoped correction in semantics.c, snapshotted under task4-before before edit.
Require focused VS17/PS>255 regression RED and preserve all physical constraints.
Initial Task4 preimplementation missing-exporter RED also not captured; report
must distinguish genuine later integration RED from unavailable initial proof.
Slot correction focused RED code6/offset884, GREEN host/ASAN/UBSAN/PPC at
.artifacts/shader-artifact/20260925T214706360Z. Next real-pair RED found writer
copying PS color output physical slot0, while wire79 requires absent sentinel.
Task4 focused .NET assertion expectedFFFFFFFF/actual0 reproduced this. Parent
authorized minimal direction/stage-aware serializer mapping; snapshot saved
task4-before/builder/shaders/Xbox360ShaderArtifactSerializer.cs. Snapshot stays
immutable; expected manifest must derive normative mapping independently.
Task4 stable handoff: worker All exit0 at20260925T220106808Z,109.NET/108Python.
Parent fresh All exit0 at .artifacts/shader-artifact/20260925T220621884Z:
real3 exports match snapshot manifest, host/ASAN/UBSAN pass, fourPPCobjects.
Parent fresh full.NET:109passed/0failed/0skipped,exit0. Output includes shared
helengine.core CS0414/IDE0040 warnings (assets/input/model), outside task scope;
no warnings suppressed or shared-source fixes. Python parent108passed above.
Shared SpriteBatch HLSL SHA256 unchanged3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2.
HEAD remainsac5be3d7e9e2e3b4e37378853e45b70ae19ca9ad; git worktree list shows
only existing main checkout. Task4 reviewer Pauli01a0da9a-dcc1-78e0-826a-d6f4057cfbda,
Terra xhigh, scoped task4-review.diff plus explicit reviewbrief. Review pending.
Task4 scoped verdict: specCOMPLIANT/qualityAPPROVED; no critical/important/minor
findings. Reviewer verified independent snapshot-manifest oracle, real exports,
all-field C comparison, narrow production fixes, isolation and explicit limits.
All four tasks COMPLETE. Final parent evidence:109.NET/108Python, CAll including
realpairs and512fixedseedmutations under host/ASAN/UBSAN, four PPC objects.
Process deviations retained: Tasks3/4 lacked initial preimplementation RED;
later reproduced bugs have genuine RED/GREEN evidence. Shared build warnings
recorded above; no pristine-build claim. No commits, worktrees, Xenia, runtime
integration or independent whole-branch review. Next separate slice is cooker/
runtime consumption; RoundedShape and GPU validation are not enabled here.
