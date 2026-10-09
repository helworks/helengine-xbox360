# Xbox 360 shader artifact Task1 report

Status: DONE

## Scope

Task1 only: immutable artifact association, deterministic managed serializer,
wire constants/helpers, independent 668-byte golden fixture, and managed tests.
No Engine/PS3/runtime call-site edits, cooker, reader, GPU validation, emulator,
UI, commits, or worktrees.

## Independent fixture evidence

The fixture words are specified independently by the wire appendix as VS
`12345678 01020304 A0B0C0D0` and PS `87654321 04030201 0D0C0B0A`, in compiler
big-endian digest order. Python `hashlib.sha256` produced:

- VS bytecode SHA-256: `1951763138c86ef2059da0f8cfdd380310c4f0ae4d89a88e6bb163a013a749bb`
- PS bytecode SHA-256: `87fba55c4bf7c7ef9b8e681b2a2a6c3946967469f31997ed644db73c0b3e34b9`
- Artifact SHA-256 with header bytes 32..63 zeroed: `955313d63bf7a11b633805695ad5fcd19c5ca21b4f7d139fa6b4548be25d0cdd`

Fixture manifest, literal hex, and checked binary are present under
`tests/shaders/artifacts/`. The materializer is read-only by default and its
explicit `--write` mode converted the reviewed literal. It verified all three
independent digests, compiler-order preimages, file lengths, strings and final
zero padding. The fixture is format-only synthetic data and must never be
executed on a GPU.

## TDD and verification evidence

RED: before production API implementation, the requested focused command
failed compilation at `Xbox360ShaderArtifactTestSupport.cs` because
`Xbox360ShaderArtifact` did not exist (CS0246). This was the expected missing
API failure. Initial fixture conversion caught a 670-byte transcription; the
literal was corrected before materialization.

GREEN: focused command
`dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --filter FullyQualifiedName~Xbox360ShaderArtifactSerializerTests`
passed 7/7, 0 skipped. The first non-elevated retry hit a shared `.artifacts`
builder DLL write denial (CS2012); the user-authorized elevated test identity
ran it successfully. Final full suite command
`dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore`
passed 102/102, 0 skipped. Final
`python tools/shader-probe/materialize_artifact_fixture.py --verify` passed.
`git diff --check` reported only pre-existing line-ending warnings in unrelated
dirty files; it reported no whitespace errors.

## Changed paths

Task1-owned changed paths:

- `builder/shaders/Xbox360ShaderArtifact.cs`
- `builder/shaders/Xbox360ShaderArtifactSerializer.cs`
- `builder/shaders/Xbox360ShaderArtifactWire.cs`
- `builder.tests/Xbox360ShaderArtifactSerializerTests.cs`
- `builder.tests/Xbox360ShaderArtifactTestSupport.cs`
- `tests/shaders/artifacts/minimal-v1.hex`
- `tests/shaders/artifacts/minimal-v1.x3sp`
- `tests/shaders/artifacts/minimal-v1.md`
- `tools/shader-probe/materialize_artifact_fixture.py`
- `docs/superpowers/plans/x360-artifact-task1-report.md`

## Concerns and limitations

The 1 MiB size guard is tested directly through a test-only reflection helper:
the v1 table-count and 256 KiB string-pool limits make that guard unreachable
from a valid bounded snapshot. Strict UTF-8 and string-pool caps are exercised
through normal serialization inputs. No GPU validation claim; integrity hashes
provide integrity only, not authenticity or full ISA validation. Task1 does
not implement the C reader or runtime integration.

## Review fix round 1

The requested snapshot at
`.artifacts/x360-artifact-task1-fix1-before/` already existed. SHA-256 checks
confirmed its serializer and two Task1 test files exactly matched the pre-fix
working files; the snapshot was preserved.

RED: added a PS used-access reference assertion to
`Rich_pair_preserves_profiles_fetch_resources_constants_and_big_endian_hashes`.
The focused command
`dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --filter FullyQualifiedName~Rich_pair_preserves_profiles_fetch_resources_constants_and_big_endian_hashes`
failed as intended: expected absolute declaration offset 996 (after the three
VS declarations), actual 924 (the first VS declaration).

GREEN: access emission now includes the stage's declaration-table start before
the local declaration index. The focused serializer suite passes 12/12, 0
skipped. Added malformed-snapshot rejection cases for stage microcode word
limits, interface-table limits, aggregate/per-stage resources, per-stage
declared constants, and per-stage used accesses. The focused Task1 suite command
passed 12/12, 0 skipped. One full .NET run for this round,
`dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore`,
passed 107/107, 0 skipped.
