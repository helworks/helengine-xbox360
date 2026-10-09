# Phase 2e: managed shared backend and registry integration

Read this first, plus x360-shader-host-protocol-brief.md for the frozen wire
schema. Implement only these host-side responsibilities; no cooking/runtime.
Existing checkout, no worktrees/commit/subagents/emulator/UI. You are not alone;
preserve concurrent native-cache edits in Xbox360PlatformAssetBuilder.cs.
TDD and apply_patch. Do not edit shared Engine or Python compiler files.

## Ownership

New `builder/shaders/Xbox360ShaderBackend.cs`, `Xbox360ShaderCompiler.cs`,
`Xbox360ShaderCompilerOptions.cs`, `Xbox360ShaderCompileResult.cs` (small
additional helper only if needed for focused responsibility), new
`builder.tests/Xbox360ShaderBackendTests.cs` and small process-test fixtures.
Minimal changes to builder csproj, existing builder registry declaration/method,
test csproj reference. New `docs/x360-shader-backend.md`.
Prefer changing the existing builder declaration to `partial` and adding the
contributor method/interface in new `builder/shaders/Xbox360PlatformShaderBackendRegistration.cs`
so concurrent native-build method bodies need no edits.

## Shared backend

Implement IShaderBackend and IShaderBackendCachePolicy (`UseSharedCache=false`),
Target Xbox360, ShaderModel4.0 only, vertex/pixel only, no ray tracing. Validate
target/model/stage and nonzero BindingPolicy.DefaultSpace explicitly, no fallback.
Compile by invoking the Python backend_protocol exactly once per request; reuse
its actual reflected layout and binary. Preserve source/program/entry/variant,
defines/options/logical binding policy. Reject duplicate define names rather than
silently losing IReadOnlyList entries when converting to protocol's map.

Success returns shared ShaderCompileResult with true success, raw big-endian
Xenos bytecode, actual reflected bindings/members/interfaces and request variant.
Use a host-only Xbox360ShaderCompileResult subclass to carry native metadata JSON,
tool/compile identity and included-source provenance for the future cooker. Never
serialize this as the final guest ABI yet. No placeholder values/empty reflection
for resources that are present in the compiled module.

Map resource kinds to shared ConstantBuffer/Texture2D/Sampler; preserve raw set
(currently0), calculate logical slot using request binding shift with checked
arithmetic. Physical slots remain in native metadata, not logical shifted slots.
Reflection interfaces use shared semantic `LOCATION` and index=SPIR-V location;
BuiltIn0 maps `SV_POSITION`, index0. This is an explicit location ABI, not a claim
to reconstruct arbitrary original HLSL semantics. Formats stay actual floatN.

Successful frontend warnings become ShaderCompileDiagnostic(Warning, full text,
source path,0,0) without losing include paths within text. On compiler/protocol/
missing-tool/unsupported shader failure throw a descriptive compilation exception
(InvalidOperationException subclass permitted), carrying original diagnostic and
request source/program/entry/stage/variant. This follows existing backend failure
convention; never invent a nonempty dummy binary to construct a failure result.

## Process lifecycle and configuration

Use ProcessStartInfo.ArgumentList, UseShellExecute=false, CreateNoWindow=true,
UTF-8 redirected stdin/stdout/stderr. No shell concatenation/foreground window.
One process per compile; no global mutable request state or disk-output cache.
Service requests therefore really recompile after any changed include/tool state.
Implement bounded newline reads (8MiB UTF-8 messages), include count<=4096,
matching IDs/protocol/message kinds, exit-code and final-result validation.
Drain stderr concurrently with bounded retained diagnostics so pipes cannot block.
Reject malformed/truncated/oversized base64, empty/nonword binary, >1024dwords,
missing metadata/reflection/identities and inconsistent result stage/entry.
Limit process/protocol wait by configured positive timeout, default60seconds.
On timeout/protocol/exit failure dispose streams/process and terminate ONLY the
child this Compile call created if still running. Never enumerate/kill other
processes or emulator. Document synchronous resolver limitation if a caller's
resolver itself blocks; don't claim ability to cancel arbitrary callback code.

Options allow explicit Python executable, protocol script path, shaderc DLL and
timeout for tests/callers. Defaults Python from HEL_X360_PYTHON or `python`, DLL
from HEL_X360_SHADERC or SHADERC_SHARED_LIBRARY, script relative to the BUILDER
ASSEMBLY directory (`shader-tools/shader-probe/backend_protocol.py`), not editor
AppContext or current working directory. Missing tools are checked at Compile,
not discovery/registry construction. No implicit DLL search in sibling platforms.

## Build and registration

Add project reference to host helengine.shader.compilation using HelengineRoot.
Copy minimal Python tool set to builder output preserving directory layout:
shader-tools/xenos_asm.py and shader-tools/shader-probe/{backend_protocol,
backend_layout,shader_probe,lower_xenos,spirv_parser}.py. No native dependency in
core/runtime. Test project references builder and retains shared contract tests.

Implement IShaderBackendRegistryContributor on existing Xbox360PlatformAssetBuilder
and register the backend lazily. Do NOT implement IShaderCompileTargetProvider or
select Xbox for legacy material cooks yet. No material serializer/cook feature
switch, no shared capabilities expansion, no Standard support claim.

## Tests

- RED missing backend API first, then targetedGREEN and full local .NET suite.
- Real Python/shaderc integration for VS transform and texturedPS fixtures,
  non-main entry, defines, nested conditional includes, changed include bytes,
  include-derived reflection, native metadata/identities, expected binary hashes.
- Registry contribution doesn't require installed DLL until compile, publishes
  Xbox target, and leaves builder without a target-provider interface.
- Exercise through real shared ShaderCompileService with throwing cache to prove
  opt-out, same request after resolver content changes yields fresh bytecode;
  failed compile never provides/reuses previous result. Source/option/variant
  changes preserve request identity; unsupported options may fail explicitly.
- Reflection assertions: cbuffer names/member offsets/types/size, texture/sampler
  shifted logical slots distinct from physicaltf1, input/output location/formats.
- Target/stage/model/policy/duplicate-defines rejection and contextual diagnostics.
- Process fixtures for malformed response, premature EOF/nonzero exit and bounded
  timeout; prove no successful result/left-running OWN child, no real emulator.
- Tool copies exist in actual builder output. Keep default model capability honest.

Tests use installed Python and shaderc DLL explicit path from protocol brief;
fail clearly if integration prerequisite missing, no silent skip counting as pass.
Use `-p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts` for local tests
to avoid sandbox shared obj writes. Do not rerun whole shared editor suite here.
Report docs/superpowers/plans/x360-shader-managed-backend-report.md with exact
RED/GREEN/fullsuite commands/results, changed paths and limitations.

## Parent evidence

Verify graph shared Engine generation2026-09-23T13:53:46Z; relevant paths all
metadata_changed then read actual source. IShaderBackend Compile(request,resolver)
returns ShaderCompileResult; IShaderBackendRegistryContributor has
RegisterShaderBackends(ShaderBackendRegistry), registry.Register(backend).
ShaderProgramDefinition constructor(name,stage,entry,ShaderBinding[],
ShaderVertexElement[],ShaderVertexElement[],ShaderVariant[]); ShaderBinding
(name,type,set,slot,size,ShaderConstantMember[]); ShaderConstantMember
(name,type,offset,size); ShaderVertexElement(semantic,index,format).
ShaderCompiledBinary requires nonempty bytes, hence failures use exceptions.
Shared ShaderBackendBase uses original-source regex metadata and cannot correctly
reflect included declarations here; don't reuse that metadata path.
Shared cache opt-out already implemented/tested8tests, no changes required there.
