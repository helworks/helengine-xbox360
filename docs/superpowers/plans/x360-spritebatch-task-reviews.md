# Shared SpriteBatch task review record

## Task1: optimized reflection

Reviewer Kuhn (Luna high) reviewed the task-only snapshot diff. Initial verdict
NEEDS FIX: OpLine/OpNoLine minimum-operand checks allowed malformed extra operands
to reach the metadata skip. Implementer Nash added exact arity3/0 checks and
RED/GREEN regressions. Scoped re-review PASS, finding ADDRESSED, no implementation
breakage apparent. Name preservation retains performance optimization, keeps
debug request identity separate, and rejects absent required reflection names.

Minor caveat recorded: synthetic arity fixtures do not prove full SPIR-V placement
validity; real optimized compiler-output tests cover valid directives separately.
No full ISA validation is claimed.

Evidence: optimization-report.md; optimization-review.diff and
optimization-fix-review.diff, all with prefix x360-spritebatch- in this directory.
Parent before fix:96Python/80.NET passed. Parent after fix:3parserarity tests and
1unsupported-runtime/debug-line test passed. Later tasks rerun full Python suite.

## Task2: explicit Python layouts and shared matrix transform

Reviewer Meitner (Terra high), task-only snapshot diff: spec PASS, quality PASS,
no actionable findings. Strict normalized profiles participate in both-stage
identity; immutable profile maps preserve sparse offsets and full batch stride.
Four masked dp4 instructions are covered by the real shared VS numeric test
with independent literal results, non-symmetric matrix and two vertices.
Parent full Python108/108 passed after this followup. Shared HLSL SHA256 remains
3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2.
Evidence: x360-spritebatch-layout-report.md and corresponding layout-review.diff.

## Task3: managed profile boundary

Reviewer Mencius (Terra high): spec PASS, quality PASS, no actionable findings.
Explicit request profile is required/matched at mapper boundary; snapshot retains
selection; factory checks exact physical fetches independently of PS varyings.
Real shared pair, identity separation and malformed profile/fetch tests covered.
Parent fresh results:95/95 .NET and108/108 Python, both exit0. Shared HLSL hash
unchanged. Cross-task Python semantics covered by Task2 review and suite, not
re-reviewed here. No GPU/render proof; external shared sources remain read-only.
Evidence: x360-spritebatch-managed-report.md and managed-review.diff.
