# Artifact Task1 brief

Read this first. Implement only this task; spec and wire appendix are approved.
Read both docs/superpowers/specs/2026-09-25-x360-shader-artifact-{design,wire}.md completely.
No subagents. Preserve other agents' edits in this dirty checkout. Use apply_patch and TDD.
Write full evidence/report to docs/superpowers/plans/x360-artifact-task1-report.md.
Do not mark report DONE until changes/tests are final; no edits after final handoff.

## Global Constraints

- Existing checkout only; no worktrees, commits, emulator, captures or desktop automation.
- Preserve unrelated dirty work. Shared Engine/PS3 sources and existing runtime call sites are read-only.
- One file contains exactly one VS and one PS; target8, version1, little-endian integers and microcode words, explicit decoding on PowerPC.
- Maximum file1048576 bytes; header64, directory8 records of16 bytes; section layouts/sentinels/order exactly as the wire appendix.
- Writer consumes explicit asset ID and validated snapshots; never infer asset identity/profile/shader behavior from filenames.
- Keep declared and used constants separate, VS/PS banks0/256 and vf0/tf1 independent. Do not invent constant values or unused matrix orientation.
- No cooker, material loader, ring emission, new dependencies, RoundedShape/Standard capability claim or GPU validation claim.
- Pure C parser: no allocation/I/O/logging/platform calls. Input stays alive and immutable while a view exists. Output view cleared on failure.
- Hashes mean integrity, not authenticity or full ISA validation. No raw C/C# struct serialization.
- Independent whole-branch review requires separate user approval; task-scoped reviews are part of this workflow.

## Review Focus

1. Valid-checksum malicious metadata must reach and fail semantic validation, not merely checksum validation (Tasks2/3).
2. A 32-bit or unaligned caller must never trigger overflowing arithmetic, unaligned loads or partial-view publication (Tasks2/3).
3. Compiler big-endian digest semantics must survive little-endian file encoding (Tasks1/2/4).
4. Sparse Batch2D inputs, unused matrix declarations and different PS profile provenance must not become accidental rejections or invented data (Tasks1/3/4).
5. Repeated strings must reuse exact whole spans; UTF-8 aliases, bad code points and hidden trailing payload must fail (Tasks1/3).


## Task 1 â€” Deterministic managed writer and independent golden fixture

**Create:**
- `builder/shaders/Xbox360ShaderArtifact.cs`: immutable asset ID/pair association.
- `builder/shaders/Xbox360ShaderArtifactSerializer.cs`: deterministic encoding.
- `builder/shaders/Xbox360ShaderArtifactWire.cs`: local constants and checked byte helpers; no shared Engine enums on wire.
- `builder.tests/Xbox360ShaderArtifactSerializerTests.cs`.
- `builder.tests/Xbox360ShaderArtifactTestSupport.cs`: controlled test-only snapshot construction, fixture path and independent field reads.
- `tests/shaders/artifacts/minimal-v1.hex`, `minimal-v1.x3sp`, `minimal-v1.md`.
- `tools/shader-probe/materialize_artifact_fixture.py`: literal hex conversion/hash verification only; no production serializer import.

**Consumes:** `Xbox360ShaderProgramPair`, its stage snapshots, both specification files.
**Produces:**

```csharp
public sealed class Xbox360ShaderArtifact {
    public Xbox360ShaderArtifact(string assetId, Xbox360ShaderProgramPair programs);
    public string AssetId { get; }
    public Xbox360ShaderProgramPair Programs { get; }
}
public static class Xbox360ShaderArtifactSerializer {
    public static byte[] Serialize(Xbox360ShaderArtifact artifact);
}
```

- [ ] Read all record definitions. Check that every snapshot field is preserved,
  deterministically derived or explicitly omitted (source path only). Map enum
  values explicitly; validate strict UTF-8 with `new UTF8Encoding(false,true)`.
- [ ] Author the literal fixture from the appendix's668-byte recipe. Resolve
  stage/file hashes using Python `hashlib` over independently specified bytes.
  Materializer defaults to verification, not overwrite. Its explicit `--write`
  mode materializes the binary from reviewed hex; ordinary tests only read it.
  Record byte offsets, digest preimages/results and synthetic non-GPU limitation.
- [ ] Write RED tests. Use a test-only reflection helper for the minimal snapshot,
  not a new public mutable constructor or production fixture fallback:

```csharp
[Fact]
public void Minimal_pair_matches_independent_wire_bytes() {
    var artifact = Xbox360ShaderArtifactTestSupport.MinimalArtifact();
    byte[] expected = File.ReadAllBytes(Xbox360ShaderArtifactTestSupport.GoldenPath);
    byte[] actual = Xbox360ShaderArtifactSerializer.Serialize(artifact);
    Assert.Equal(668, actual.Length);
    Assert.Equal(expected, actual);
    Assert.Equal(new byte[] { 0x78, 0x56, 0x34, 0x12 }, actual[560..564]);
}
```

- [ ] Run the focused command below and capture missing-API RED before adding code.
- [ ] Implement in two passes: canonical record/string collection and checked
  size/offset assignment; then exact-sized buffer emission. Use checked arithmetic
  before allocation, `BinaryPrimitives.WriteUInt32LittleEndian`, raw decoded digest
  bytes, zero reserved/padding fields and `SHA256.HashData` for integrity. Input
  snapshots must remain unchanged. Return a fresh independent array each call.
- [ ] Add tests for empty/null/too-long/NUL/invalid-surrogate asset and labels;
  empty variant; multibyte strings near4096-byte limit; reused full strings versus
  substrings; collection permutations; null arguments and rejected file-size cap.
  A synthetic oversized-input builder may use reflection only inside tests.
- [ ] Assert all profile/resources/constants fields by independent little-endian
  test reads, including PS profile differing from VS, logical shifts above255,
  bank256, used range versus unused matrix and both matrix orientations. Verify
  stage hashes against original BE words, not LE file slices. No C# production
  deserializer is required just to test the serializer.
- [ ] Run focused and full.NET suites; verify golden hex/binary consistency.
  Report RED/GREEN and exact files. No commit.

```powershell
dotnet test builder.tests/helengine.x360.builder.tests.csproj -p:ArtifactsPath=C:/dev/helworks/helengine-x360/.artifacts --no-restore --filter FullyQualifiedName~Xbox360ShaderArtifactSerializerTests
python tools/shader-probe/materialize_artifact_fixture.py --verify
```
