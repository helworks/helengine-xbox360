# Phase 3a task review

Reviewer: Sagan, Luna xhigh. Scope: the eight new production/test/layout files
in `x360-shader-program-pair-review.diff`, not the whole dirty checkout and not
a standalone independent review. Parent fresh verification: 80/80 tests, exit0.

Verdict: no functional correctness blockers; substantially conforms to Phase3a.
Snapshots copy and protect collections; explicit big-endian word decoding,
metadata/reflection validation and bounded pair costs match the intended scope.
No serialization, runtime activation or material selection was introduced.

Minor finding: `builder.tests/Xbox360ShaderProgramTestSupport.cs:9` hardcodes
the shaderc DLL in a sibling checkout. Existing integration tests use the same
prerequisite, so this is not a failure in the verified environment. A future
shared/configurable test prerequisite should replace duplicated local paths.
Deferred rather than expanding this task into existing test infrastructure.

Focused external contract check: ShaderCompileRequest.cs:44 rejects an empty or
whitespace variant and preserves the supplied value; the factory's nonempty
variant requirement is compatible. Documentation's SV_TARGET0/LOCATION0 typo
was corrected after the diff snapshot; HLSL uses the former, host reflection
the latter. No production change resulted from that documentation correction.

Upstream limitation, not a defect hidden by the tests: a real uniform-only PS
can report register_count=0 and is rejected by the explicit 1..64 contract.
The bank256 test uses a controlled valid header solely to isolate layout
validation; it is not execution evidence for that shader. No full ISA safety
claim is made. Reviewer did not rerun the suite or mutate the checkout.
