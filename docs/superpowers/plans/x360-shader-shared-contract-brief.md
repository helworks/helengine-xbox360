# Phase 2b: target identity and opt-out cache contract

Implement this bounded task only. Work in the existing checkout, no worktrees,
commits, emulator or desktop control. No subagents. You are not alone: preserve
all unrelated/concurrent changes in both repositories. Use apply_patch and TDD.

## Ownership

New local `builder.tests/helengine.x360.builder.tests.csproj` and focused
`builder.tests/ShaderSharedContractTests.cs` (plus a small test helper if needed).
Use net9.0 xUnit, same cached versions as shared editor tests: Test.Sdk17.14.1,
xunit2.9.3, runner3.1.4. Reference shared helengine.shader.compilation via
HelengineRoot override convention. Do not alter existing builder production code.

Shared Engine root C:/dev/helworks/helengine. Only these shared files:
- engine/helengine.shader/shaders/runtime/ShaderCompileTarget.cs
- engine/helengine.shader/shaders/runtime/ShaderTargetDescriptors.cs
- engine/helengine.shader/shaders/compilation/ShaderCompileService.cs
- new engine/helengine.shader/shaders/compilation/IShaderBackendCachePolicy.cs

Shared paths are outside the normal writable root: request escalation if needed,
using apply_patch (the apply_patch CLI is available for an escalated invocation).
Never bypass permission denial with a different writer. If escalation unavailable,
prepare a patch artifact locally and report exact blocker; parent applies it.

## Requirements

Append `Xbox360 = 8` after existing `Ps3` (ordinal7), preserving all existing
values. Append descriptor `x360` / `HEL_API_X360`. Current PS3 target changes are
committed as b806163d; parent verified exact shared paths clean. User answered
"eu acho que ja foi?" to PS3-coordination check. Parent is proceeding with this
minimal append-only change; recheck exact status before editing and escalate any
new overlapping change rather than overwrite it.

Add optional host-only `IShaderBackendCachePolicy` with `bool UseSharedCache { get; }`.
In ShaderCompileService.Compile, after GetBackend and before creating/looking up
the key, if backend implements this policy with false, call backend.Compile and
return directly. Do not look up or store any result for opt-out. Backends without
this interface (and explicit true) retain existing behavior. No changes to cache
key format or other backends. This is not a claim the Xbox backend exists yet.

## Acceptance

- TDD RED before implementation and GREEN afterward.
- Enum ordinals0..7 preserved, Xbox3608, descriptor roundtrip and unique complete
  descriptor table, Ps3 descriptor unchanged.
- Opt-out test uses a throwing cache to prove no lookup/store; repeated requests
  invoke backend twice. Compile changed include content/options/variant/tool
  state through same service and verify backend receives fresh inputs, no stale
  result (a recording test backend is appropriate for service behavior).
- Failed result from opt-out is not stored/reused; later successful invocation
  calls backend again. Default backend and explicit-true backend still hit cache.
- Null request and unregistered-target failure remain intact.
- Run focused new .NET tests and shared ShaderTargetDescriptorTests plus
  ShaderProjectBoundaryTests if shared editor project builds without unrelated
  blockers. Report blockers honestly; do not fix unrelated shared issues.
- No native dependencies enter helengine.shader runtime: compilation project
  already links compilation/**/*.cs; leave that boundary intact.

## Source/graph evidence

Parent Verify graph in C-dev-helworks-helengine, generation2026-09-23T13:53:46Z.
Coverage for target/descriptors/service and ShaderTargetDescriptorTests reports
metadata_changed; parent read full actual source. ShaderCompileService currently
always creates key then cache.TryGet then backend.Compile then cache.Store.
Constructor requires resolver/cache/hasher. Shared tests in
engine/helengine.editor.tests/ShaderTargetDescriptorTests.cs enumerate every enum
and descriptor (not fixed count). ShaderProjectBoundaryTests covers host/runtime
assembly separation. No ShaderCompileService tests found by bounded graph search
and editor.tests source grep (not an exhaustive repository-wide claim).

Report path: docs/superpowers/plans/x360-shader-shared-contract-report.md.
Include commands, RED/GREEN evidence, exact shared paths edited and tests/results.
Return concise DONE/DONE_WITH_CONCERNS/BLOCKED/NEEDS_CONTEXT; no commit.
