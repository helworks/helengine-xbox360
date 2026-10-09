# Task4 scoped review gate

Read the approved Task4 implementation brief, design and wire appendix, then
the implementer's final report and task-only review diff. This is a scoped
task review, not an independent whole-branch review. No edits, subagents,
worktrees, commits, emulator or desktop automation. Preserve the dirty tree.

The review diff is a snapshot delta, not HEAD-to-working-tree: prior work is
out of scope. Follow the subagent-driven-development task-reviewer-prompt.
Do not rerun full suites; parent runs fresh final acceptance. Read unchanged
dependencies only for a concrete named risk, with bounded reads.

Check these acceptance boundaries explicitly:

- Exporter produces three real compiler outputs for mesh and both shared
  SpriteBatch Textured optimization modes, resolving actual catalog/source.
- Expected manifest comes from immutable compiler snapshots, never reading
  serialized X3SP bytes or using reader output as its oracle.
- C integration compares all preserved pair/stage/interface/resource/
  declaration/access/string fields and words, original BE hashes and derived
  costs 110/94/94; no optimized/unoptimized whole-file equality assumption.
- Synthetic contract fixtures remain labeled and separate from real outputs;
  both-stage buffers, unused matrices, sparse Batch2D location5, logical slots
  above255, Unicode and mixed VS/PS profiles are covered across C# and C.
- Authorized cross-task fix: semantics.c incorrectly required cbuffer logical
  slot0. Actual compiler uses shifted logical17; wire fixes logical SET and
  physical set/binding, not logical slot. Check narrow removal of this extra
  rejection, regression for VS17/PS>255, preserved physical/fetch/bank checks.
  This existing file has its own Task4 before snapshot and belongs in the diff.
- Second authorized cross-task fix: serializer copied host PS color output
  physical slot0, but normative wire79 requires absent. Check stage/direction
  mapping only for PS output, leaving PS inputs/VS outputs and immutable source
  snapshot unchanged. Exporter oracle applies the normative mapping separately,
  without calling serializer helpers or reading serialized bytes. Serializer
  before snapshot is also in Task4-before and its delta belongs in this review.
- Determinism covers repeat serialization, order permutations and explicit
  asset identity changes without bytecode changes.
- Bounded fixed-seed mutation corpus treats crashes as failures. Repaired
  checksum cases reach semantic checks and preserve error-category assertions.
- Harness fails missing prerequisites/nonzero subprocesses, preserves pinned
  offline Docker, container-local sanitized executables, non-PIE ASAN,
  bounded probes/test runs, source readonly, checkout-contained output, logs.
- Docs link normative wire appendix rather than introducing competing offsets.
  No cooker/runtime/ISA/GPU capability claim or shared-source changes.

Inherited verified behavior: Task2 envelope hashes both stage code ranges
before Task3 semantics, using logical words reencoded BE and comparing the
stored digest at stage+120. Parser calls envelope first. Do not mistake
unchanged dependency behavior for an omission or request duplicate hashing.
Task3's initial executable preimplementation RED was not captured; this
process limitation is already documented, with later genuine RED fixes.

Return Spec Compliance, Strengths, Issues by severity with file:line evidence,
and Task Quality Approved/Needs fixes. Explicitly separate a missing required
feature from optional test polish and from unverified cross-task claims.
