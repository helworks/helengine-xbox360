# Task3 — managed explicit Batch2D profile and real shared pair

Read this first. Dispatch only after Task2 accepted. Existing checkout only,
no commits/worktrees/subagents/emulator/UI or shared Engine/PS3 writes. Preserve
unrelated/concurrent changes, notably native-cache builder edits and runtime UI.
Implement exactly this host-side scope with TDD/apply_patch.

Own new builder/shaders/Xbox360ShaderVertexLayout.cs and
builder.tests/Xbox360SpriteBatchShaderTests.cs. Modify local compiler options,
compiler, compile result, protocol mapper, program snapshot/factory and relevant
local test helpers/fixture. csproj change ONLY to copy vertex_layout.py (preserve
all other content). docs/x360-shader-program-layout.md and backend/protocoldocs
as needed. No Python semantic changes: Task2 established the interface.

Local enum Xbox360ShaderVertexLayout { Mesh3D, Batch2D }. Append optional compiler
options argument vertexLayout=Mesh3D, reject invalid enum before launching tools.
Serialize mesh3d-v1/batch2d-v1 via request.vertex_layout. Request shared type and
IShaderBackend stay unchanged. Mapper receives expected local profile and REQUIRES
metadata.vertex_layout to match it, rejecting missing/unknown/mismatched replies.
Typed compile result preserves selection, factory validates profile-specific
fetches, immutable vertex program snapshot exposes profile for future serializer.
Do not infer requested profile solely from worker output. Existing generic
backend callers remain mesh-default; pixel varying validation is not vertex-fetch
validation and must not be constrained to the profile's input widths.

Profiles: mesh vf0 stride12 floats, loc0float3/off0 loc1float3/off3
loc2float2/off6 loc3float4/off8. Batch vf0 stride24floats, loc0..5float4,
offsets0/4/8/12/16/20. Existing tf1, VSbank0,PSbank256 unchanged.
Task2 successful protocol metadata reports vertex_layout for bothstages;
normalized profile is part of compile identity even if PS binaryidentical.

TDD integration reads actual shared SpriteBatchShaderVariants.All, selects
Textured with its entrypoints/defines and reads unmodified
C:/dev/helworks/helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl.
Do not embed source/catalog replacement. Missing real shaderc/source is an
explicit prerequisite failure, not a skip/pass. Reuse current DLL configuration;
do not introduce new dependency installation. Compile via actual bundledPython
backend with Batch2D config, optimize=false and true, then FromCompilation +Link.
Assert exact native layout, VS projection64bytes/fourconstantregs, PSnoCB,
texture/sampler tf1, VS/PS varyingwidths/slots. Observe actualwordcounts and
handderive paircost expectations; do not copy oldfixturecost110 blindly.

Add malformed reply tests: missing/unknown/wrongprofile, invalid options enum,
wrongwidth/offset/stride/fetch and outofrangelocation, retained immutable profile.
Process fixtures must echo correct requestedprofile for normalcases and provide
explicit corruption modes. Preserve their timeout/cleanup tests (don't weaken).
Constructor-cloning testhelpers must be updated if resultconstructor changes;
avoid tests that silently default a cloned Batch2D result toMesh3D.
Bundledtools expectations include vertex_layout.py, hashes accountforactualbytes.

Assert unchanged legacy binaryhashes:
VS40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF
PSD70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A.
Arbitraryprogramnames must notselectlayout; request uses exactsharedvariantdata.
Genericbackend is NOT a hardcoded catalogdispatcher. RoundedShapePS remains
explicitly unsupported; no approximation, fallbackshader or runtimeactivation.

Run focusednewtests then fullsuite:
dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore
Reportallfailures, includingknownintermittenttimeouttest. Sharedbuildoutputs
outsidecheckoutmayrequireapprovedescalation; do not modifysharedsources.
Python suite neednotrepeat ifunchangedfromacceptedTask2 (parentfinalchecks).

Environment preflight: parent fresh80.NETtests passed after elevated restore/build.
Sandbox's old assets/cache referenced CodexSandboxOffline; current elevated
restore uses Helena's installed packages. Use same approved elevated identity
for dotnet tests ifnormalexecutioncannotwrite sourcelink/build outputs. No manual
assetsfile edits or packageupgrade; no further sourcefix was needed.

Write docs/superpowers/plans/x360-spritebatch-managed-report.md: RED/GREEN,
commands/counts, files, actualsharedsourcehash, optimized/unoptimizedpairfacts,
limits. Document no GPU/render proof. Return concise DONE/NEEDS_CONTEXT/BLOCKED.

Parent measured real shared Textured pair with explicit batch2d-v1, both optimize
false/true: VS45 dwords/5 registers, PS15 dwords/4 registers; varying slots0/1.
ProgramControl=0x00100304; ProgramUpload=45+15+6=66,
ConstantUpload=4*4+2=18, StateUpload=10, PairUpload=94 dwords.
VS SHA256 9cd3526ecd7a2016a89cabb10b1388979c58a6f62d24785d0e4206234b32882e;
PS SHA256 7c67ad650636cd7ff621c85568792ded67a3f2120a0dd3975fa78b60b22e849c.
Same hashes in both modes. Task2 review PASS; parent full Python108/108 passed.
