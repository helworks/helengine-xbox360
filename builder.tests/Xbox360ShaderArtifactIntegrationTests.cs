using System.Buffers.Binary;
using System.Security.Cryptography;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderArtifactIntegrationTests {
    [Fact]
    public void Legacy_mesh_pair_serializes_real_compiler_snapshots_at_110_dwords() {
        Xbox360ShaderCompileResult vertexResult = Xbox360ShaderProgramTestSupport.Reidentify(
            Xbox360ShaderProgramTestSupport.TransformVertex,
            programName: "task4-mesh-vs",
            entryPoint: "main",
            variant: "task4-mesh-fixture");
        Xbox360ShaderCompileResult pixelResult = Xbox360ShaderProgramTestSupport.Reidentify(
            Xbox360ShaderProgramTestSupport.TexturedPixel,
            programName: "task4-mesh-ps",
            entryPoint: "main",
            variant: "task4-mesh-fixture");

        Xbox360ShaderProgramPair pair = Xbox360ShaderProgramPair.Link(
            Xbox360ShaderProgramFactory.FromCompilation(vertexResult),
            Xbox360ShaderProgramFactory.FromCompilation(pixelResult));
        byte[] artifact = Xbox360ShaderArtifactSerializer.Serialize(
            new Xbox360ShaderArtifact("task4/legacy-mesh", pair));

        Assert.Equal(110, pair.PairUploadDwords);
        Assert.Equal(8u, ReadU32(artifact, 16));
        Assert.Equal(8u, ReadU32(artifact, 20));
        Assert.Equal(SHA256.HashData(vertexResult.Binary.Bytecode), artifact.AsSpan(240 + 120, 32).ToArray());
        uint pixelOutputOffset = ReadU32(artifact, 400 + 28);
        Assert.Equal(uint.MaxValue, ReadU32(artifact, checked((int)pixelOutputOffset + 12)));
        Assert.NotEmpty(artifact);
    }

    [Fact]
    public void Contract_fixture_serialization_is_order_independent_and_asset_id_only_changes_integrity() {
        Xbox360ShaderArtifact first = Xbox360ShaderArtifactTestSupport.RichArtifact();
        Xbox360ShaderArtifact reordered = Xbox360ShaderArtifactTestSupport.RichArtifact(reverseCollections: true);
        byte[] baseline = Xbox360ShaderArtifactSerializer.Serialize(first);
        byte[] reorderedBytes = Xbox360ShaderArtifactSerializer.Serialize(reordered);
        byte[] changedIdentity = Xbox360ShaderArtifactSerializer.Serialize(
            new Xbox360ShaderArtifact("asset/another-π", first.Programs));

        Assert.Equal(baseline, reorderedBytes);
        Assert.NotEqual(baseline, changedIdentity);
        Assert.NotEqual(baseline.AsSpan(32, 32).ToArray(), changedIdentity.AsSpan(32, 32).ToArray());
        Assert.Equal(ReadStageWords(baseline, 0), ReadStageWords(changedIdentity, 0));
        Assert.Equal(ReadStageWords(baseline, 1), ReadStageWords(changedIdentity, 1));
        Assert.Equal(0x12345678u, ReadStageWords(baseline, 0)[0]);
        Assert.Equal(0x12345678u, first.Programs.Vertex.MicrocodeWords[0]);
    }

    static uint[] ReadStageWords(byte[] bytes, int stageIndex) {
        int stageOffset = 240 + stageIndex * 160;
        int codeOffset = checked((int)ReadU32(bytes, stageOffset + 12));
        int count = checked((int)ReadU32(bytes, stageOffset + 16));
        return Enumerable.Range(0, count)
            .Select(index => ReadU32(bytes, codeOffset + index * sizeof(uint)))
            .ToArray();
    }

    static uint ReadU32(byte[] bytes, int offset) =>
        BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset, sizeof(uint)));
}
