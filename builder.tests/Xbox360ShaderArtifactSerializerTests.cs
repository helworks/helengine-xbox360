using System.Buffers.Binary;
using System.Text;
using System.Security.Cryptography;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderArtifactSerializerTests {
    [Fact]
    public void Minimal_pair_matches_independent_wire_bytes() {
        Xbox360ShaderArtifact artifact = Xbox360ShaderArtifactTestSupport.MinimalArtifact();
        byte[] expected = File.ReadAllBytes(Xbox360ShaderArtifactTestSupport.GoldenPath);

        byte[] actual = Xbox360ShaderArtifactSerializer.Serialize(artifact);

        Assert.Equal(668, actual.Length);
        Assert.Equal(expected, actual);
        Assert.Equal(new byte[] { 0x78, 0x56, 0x34, 0x12 }, actual[560..564]);
        Assert.Equal(0u, Xbox360ShaderArtifactTestSupport.U32(actual, 200));
        Assert.Equal(0u, Xbox360ShaderArtifactTestSupport.U32(actual, 204));
    }

    [Fact]
    public void Serialization_returns_fresh_arrays_and_preserves_snapshot_words() {
        Xbox360ShaderArtifact artifact = Xbox360ShaderArtifactTestSupport.MinimalArtifact();
        uint[] originalWords = artifact.Programs.Vertex.MicrocodeWords.ToArray();

        byte[] first = Xbox360ShaderArtifactSerializer.Serialize(artifact);
        first[0] = 0;
        byte[] second = Xbox360ShaderArtifactSerializer.Serialize(artifact);

        Assert.Equal("X3SP", Encoding.ASCII.GetString(second, 0, 4));
        Assert.Equal(originalWords, artifact.Programs.Vertex.MicrocodeWords);
        Assert.NotSame(first, second);
    }

    [Fact]
    public void Header_directory_offsets_and_integrity_are_explicit_little_endian() {
        byte[] bytes = Xbox360ShaderArtifactSerializer.Serialize(Xbox360ShaderArtifactTestSupport.MinimalArtifact());

        Assert.Equal(668u, Xbox360ShaderArtifactTestSupport.U32(bytes, 12));
        Assert.Equal(8u, Xbox360ShaderArtifactTestSupport.U32(bytes, 20));
        Assert.Equal(560u, Xbox360ShaderArtifactTestSupport.U32(bytes, 252));
        Assert.Equal(0x12345678u, BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(560, 4)));
    }

    [Fact]
    public void Rich_pair_preserves_profiles_fetch_resources_constants_and_big_endian_hashes() {
        byte[] bytes = Xbox360ShaderArtifactSerializer.Serialize(Xbox360ShaderArtifactTestSupport.RichArtifact());
        const int vs = 240;
        const int ps = 400;
        int interfaceSection = checked((int)Xbox360ShaderArtifactTestSupport.U32(bytes, 64 + 3 * 16 + 4));
        int resourceSection = checked((int)Xbox360ShaderArtifactTestSupport.U32(bytes, 64 + 4 * 16 + 4));
        int declarationSection = checked((int)Xbox360ShaderArtifactTestSupport.U32(bytes, 64 + 5 * 16 + 4));
        int accessSection = checked((int)Xbox360ShaderArtifactTestSupport.U32(bytes, 64 + 6 * 16 + 4));

        Assert.Equal(1u, Xbox360ShaderArtifactTestSupport.U32(bytes, vs + 4));
        Assert.Equal(2u, Xbox360ShaderArtifactTestSupport.U32(bytes, ps + 4));
        Assert.Equal(192u, Xbox360ShaderArtifactTestSupport.U32(bytes, vs + 44));
        Assert.Equal(12u, Xbox360ShaderArtifactTestSupport.U32(bytes, vs + 48));
        Assert.Equal(0u, Xbox360ShaderArtifactTestSupport.U32(bytes, vs + 52));
        Assert.Equal(16u, Xbox360ShaderArtifactTestSupport.U32(bytes, ps + 44));
        Assert.Equal(1u, Xbox360ShaderArtifactTestSupport.U32(bytes, ps + 48));
        Assert.Equal(256u, Xbox360ShaderArtifactTestSupport.U32(bytes, ps + 52));

        uint vsInputOffset = Xbox360ShaderArtifactTestSupport.U32(bytes, vs + 20);
        Assert.Equal((uint)interfaceSection, vsInputOffset);
        Assert.Equal(0u, Xbox360ShaderArtifactTestSupport.U32(bytes, (int)vsInputOffset + 16));
        Assert.Equal(12u, Xbox360ShaderArtifactTestSupport.U32(bytes, (int)vsInputOffset + 20));
        Assert.Equal(6u, Xbox360ShaderArtifactTestSupport.U32(bytes, (int)vsInputOffset + 24));
        Assert.Equal(2u, Xbox360ShaderArtifactTestSupport.U32(bytes, (int)vsInputOffset + 28));

        Assert.Equal(300u, Xbox360ShaderArtifactTestSupport.U32(bytes, resourceSection + 40 + 16));
        Assert.Equal(256u, Xbox360ShaderArtifactTestSupport.U32(bytes, resourceSection + 40 + 32));
        Assert.Equal(0u, Xbox360ShaderArtifactTestSupport.U32(bytes, resourceSection + 32));
        Assert.Equal(17u, Xbox360ShaderArtifactTestSupport.U32(bytes, resourceSection + 80 + 24));
        Assert.Equal(1u, Xbox360ShaderArtifactTestSupport.U32(bytes, resourceSection + 80 + 28));
        Assert.Equal(uint.MaxValue, Xbox360ShaderArtifactTestSupport.U32(bytes, resourceSection + 80 + 32));
        Assert.Equal(23u, Xbox360ShaderArtifactTestSupport.U32(bytes, resourceSection + 120 + 24));

        Assert.Equal(3u, Xbox360ShaderArtifactTestSupport.U32(bytes, vs + 60));
        Assert.Equal(2u, Xbox360ShaderArtifactTestSupport.U32(bytes, vs + 68));
        Assert.Equal(2u, Xbox360ShaderArtifactTestSupport.U32(bytes, accessSection + 12));
        Assert.Equal(16u, Xbox360ShaderArtifactTestSupport.U32(bytes, accessSection + 16));
        Assert.Equal((uint)declarationSection, Xbox360ShaderArtifactTestSupport.U32(bytes, accessSection));
        Assert.Equal(1u, Xbox360ShaderArtifactTestSupport.U32(bytes, accessSection + 24 + 12));
        Assert.Equal(16u, Xbox360ShaderArtifactTestSupport.U32(bytes, accessSection + 24 + 16));
        Assert.Equal((uint)(declarationSection + 3 * 24),
            Xbox360ShaderArtifactTestSupport.U32(bytes, accessSection + 2 * 24));

        Assert.Equal(Convert.FromHexString("1951763138C86EF2059DA0F8CFDD380310C4F0AE4D89A88E6BB163A013A749BB"), bytes.AsSpan(vs + 120, 32).ToArray());
        Assert.Equal(SHA256.HashData(Convert.FromHexString("1234567801020304A0B0C0D0")), bytes.AsSpan(vs + 120, 32).ToArray());
        Assert.Equal(SHA256.HashData(Convert.FromHexString("87654321040302010D0C0B0A")), bytes.AsSpan(ps + 120, 32).ToArray());
    }

    [Fact]
    public void Collection_permutations_produce_identical_bytes_and_full_strings_reuse_exact_spans() {
        byte[] first = Xbox360ShaderArtifactSerializer.Serialize(Xbox360ShaderArtifactTestSupport.RichArtifact());
        byte[] permuted = Xbox360ShaderArtifactSerializer.Serialize(Xbox360ShaderArtifactTestSupport.RichArtifact(reverseCollections: true));
        Assert.Equal(first, permuted);

        Xbox360ShaderProgramPair basePair = Xbox360ShaderArtifactTestSupport.MinimalArtifact().Programs;
        Xbox360ShaderProgram vertex = Xbox360ShaderArtifactTestSupport.CloneProgram(basePair.Vertex, programName: "whole-id");
        Xbox360ShaderProgramPair pair = Xbox360ShaderProgramPair.Link(vertex, basePair.Pixel);
        byte[] bytes = Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("whole-id", pair));
        uint assetOffset = Xbox360ShaderArtifactTestSupport.U32(bytes, 192);
        uint programNameOffset = Xbox360ShaderArtifactTestSupport.U32(bytes, 240 + 72);
        Assert.Equal(assetOffset, programNameOffset);
        Assert.Equal(8u, Xbox360ShaderArtifactTestSupport.U32(bytes, 240 + 76));

        Xbox360ShaderProgram substringVertex = Xbox360ShaderArtifactTestSupport.CloneProgram(basePair.Vertex, programName: "bc");
        Xbox360ShaderProgramPair substringPair = Xbox360ShaderProgramPair.Link(substringVertex, basePair.Pixel);
        byte[] substringBytes = Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("abcd", substringPair));
        Assert.NotEqual(Xbox360ShaderArtifactTestSupport.U32(substringBytes, 192),
            Xbox360ShaderArtifactTestSupport.U32(substringBytes, 240 + 72));
        Assert.Equal(4u, Xbox360ShaderArtifactTestSupport.U32(substringBytes, 196));
        Assert.Equal(2u, Xbox360ShaderArtifactTestSupport.U32(substringBytes, 240 + 76));
    }

    [Fact]
    public void Strict_utf8_limits_empty_variant_and_bad_asset_or_label_inputs_are_enforced() {
        Xbox360ShaderArtifact minimal = Xbox360ShaderArtifactTestSupport.MinimalArtifact();
        byte[] exactly4096 = Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact(new string('π', 2048), minimal.Programs));
        Assert.Equal(4096u, Xbox360ShaderArtifactTestSupport.U32(exactly4096, 196));
        Assert.Equal(0u, Xbox360ShaderArtifactTestSupport.U32(exactly4096, 200));

        Assert.Throws<ArgumentNullException>(() => Xbox360ShaderArtifactSerializer.Serialize(null));
        Assert.Throws<ArgumentNullException>(() => new Xbox360ShaderArtifact("id", null));
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact(null, minimal.Programs)));
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("", minimal.Programs)));
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("bad\0id", minimal.Programs)));
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact(new string('π', 2049), minimal.Programs)));
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("\uD800", minimal.Programs)));

        Xbox360ShaderProgram badLabel = Xbox360ShaderArtifactTestSupport.CloneProgram(minimal.Programs.Vertex, programName: "");
        Xbox360ShaderProgramPair badPair = Xbox360ShaderProgramPair.Link(badLabel, minimal.Programs.Pixel);
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("id", badPair)));

        Xbox360ShaderProgram nulLabel = Xbox360ShaderArtifactTestSupport.CloneProgram(minimal.Programs.Vertex, programName: "name\0tail");
        Xbox360ShaderProgramPair nulPair = Xbox360ShaderProgramPair.Link(nulLabel, minimal.Programs.Pixel);
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("id", nulPair)));

        Xbox360ShaderProgram longLabel = Xbox360ShaderArtifactTestSupport.CloneProgram(minimal.Programs.Vertex, programName: new string('x', 4097));
        Xbox360ShaderProgramPair longLabelPair = Xbox360ShaderProgramPair.Link(longLabel, minimal.Programs.Pixel);
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("id", longLabelPair)));

        Xbox360ShaderProgram invalidUnicodeLabel = Xbox360ShaderArtifactTestSupport.CloneProgram(minimal.Programs.Vertex, entryPoint: "\uD800");
        Xbox360ShaderProgramPair invalidUnicodePair = Xbox360ShaderProgramPair.Link(invalidUnicodeLabel, minimal.Programs.Pixel);
        Assert.Throws<ArgumentException>(() => Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact("id", invalidUnicodePair)));

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactTestSupport.InvokeFileSizeCheck(1_048_577));
        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactTestSupport.InvokeFileSizeCheck(191));
    }

    [Fact]
    public void Repeated_serialization_is_independent_and_snapshot_words_are_unchanged() {
        Xbox360ShaderArtifact artifact = Xbox360ShaderArtifactTestSupport.RichArtifact();
        uint[] beforeVertex = artifact.Programs.Vertex.MicrocodeWords.ToArray();
        uint[] beforePixel = artifact.Programs.Pixel.MicrocodeWords.ToArray();

        byte[] first = Xbox360ShaderArtifactSerializer.Serialize(artifact);
        first.AsSpan().Clear();
        byte[] second = Xbox360ShaderArtifactSerializer.Serialize(artifact);

        Assert.Equal("X3SP", Encoding.ASCII.GetString(second, 0, 4));
        Assert.Equal(beforeVertex, artifact.Programs.Vertex.MicrocodeWords);
        Assert.Equal(beforePixel, artifact.Programs.Pixel.MicrocodeWords);
        Assert.NotSame(first, second);
    }

    [Fact]
    public void Rejects_microcode_word_counts_above_stage_limit() {
        Xbox360ShaderProgramPair pair = Xbox360ShaderArtifactTestSupport.MinimalArtifact().Programs;
        uint[] tooManyWords = new uint[1026];
        Xbox360ShaderProgram vertex = Xbox360ShaderArtifactTestSupport.Snapshot(pair.Vertex, words: tooManyWords);

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactSerializer.Serialize(
            Xbox360ShaderArtifactTestSupport.Artifact(vertex, pair.Pixel)));
    }

    [Fact]
    public void Rejects_interface_table_overflow() {
        Xbox360ShaderProgramPair pair = Xbox360ShaderArtifactTestSupport.MinimalArtifact().Programs;
        Xbox360ShaderInterface[] inputs = Enumerable.Range(0, 23)
            .Select(index => Xbox360ShaderArtifactTestSupport.MakeInterface(Xbox360ShaderInterfaceKind.Location, index % 8, 4))
            .ToArray();
        Xbox360ShaderProgram vertex = Xbox360ShaderArtifactTestSupport.Snapshot(pair.Vertex, inputs: inputs);

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactSerializer.Serialize(
            Xbox360ShaderArtifactTestSupport.Artifact(vertex, pair.Pixel)));
    }

    [Fact]
    public void Rejects_resource_table_overflow_and_per_stage_resource_overflow() {
        Xbox360ShaderProgramPair pair = Xbox360ShaderArtifactTestSupport.MinimalArtifact().Programs;
        Xbox360ShaderResource[] fourResources = Enumerable.Range(0, 4)
            .Select(index => Xbox360ShaderArtifactTestSupport.MakeResource($"vsResource{index}", ShaderResourceType.Texture2D))
            .ToArray();
        Xbox360ShaderProgram fourOnVertex = Xbox360ShaderArtifactTestSupport.Snapshot(pair.Vertex, resources: fourResources);
        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactSerializer.Serialize(
            Xbox360ShaderArtifactTestSupport.Artifact(fourOnVertex, pair.Pixel)));

        Xbox360ShaderResource[] threeVertex = fourResources[..3];
        Xbox360ShaderResource[] twoPixel = [
            Xbox360ShaderArtifactTestSupport.MakeResource("psTexture", ShaderResourceType.Texture2D),
            Xbox360ShaderArtifactTestSupport.MakeResource("psSampler", ShaderResourceType.Sampler)
        ];
        Xbox360ShaderProgram boundedVertex = Xbox360ShaderArtifactTestSupport.Snapshot(pair.Vertex, resources: threeVertex);
        Xbox360ShaderProgram boundedPixel = Xbox360ShaderArtifactTestSupport.Snapshot(pair.Pixel, resources: twoPixel);
        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactSerializer.Serialize(
            Xbox360ShaderArtifactTestSupport.Artifact(boundedVertex, boundedPixel)));
    }

    [Fact]
    public void Rejects_declared_constant_count_above_stage_limit() {
        Xbox360ShaderProgramPair pair = Xbox360ShaderArtifactTestSupport.RichArtifact().Programs;
        Xbox360ShaderDeclaredConstantMember[] declarations = Enumerable.Range(0, 257)
            .Select(index => Xbox360ShaderArtifactTestSupport.MakeDeclared($"member{index:D3}", "float4", (index % 256) * 16, 16))
            .ToArray();
        Xbox360ShaderConstantBufferLayout oversized = Xbox360ShaderArtifactTestSupport.MakeBuffer(
            4096, 0, declarations, Array.Empty<Xbox360ShaderUsedConstantMember>());
        Xbox360ShaderProgram vertex = Xbox360ShaderArtifactTestSupport.Snapshot(
            pair.Vertex, constantBuffer: oversized, replaceConstantBuffer: true);

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactSerializer.Serialize(
            Xbox360ShaderArtifactTestSupport.Artifact(vertex, pair.Pixel)));
    }

    [Fact]
    public void Rejects_used_constant_access_count_above_stage_limit() {
        Xbox360ShaderProgramPair pair = Xbox360ShaderArtifactTestSupport.MinimalArtifact().Programs;
        Xbox360ShaderDeclaredConstantMember declaration = Xbox360ShaderArtifactTestSupport.MakeDeclared("member", "float4", 0, 16);
        Xbox360ShaderUsedConstantMember[] accesses = Enumerable.Range(0, 257)
            .Select(_ => Xbox360ShaderArtifactTestSupport.MakeUsed("member", 0, [0]))
            .ToArray();
        Xbox360ShaderConstantBufferLayout oversized = Xbox360ShaderArtifactTestSupport.MakeBuffer(
            16, 0, [declaration], accesses);
        Xbox360ShaderResource cbuffer = Xbox360ShaderArtifactTestSupport.MakeResource(
            "constants", ShaderResourceType.ConstantBuffer, logicalSlot: 0, bank: 0);
        Xbox360ShaderProgram vertex = Xbox360ShaderArtifactTestSupport.Snapshot(
            pair.Vertex, resources: [cbuffer], constantBuffer: oversized, replaceConstantBuffer: true);

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderArtifactSerializer.Serialize(
            Xbox360ShaderArtifactTestSupport.Artifact(vertex, pair.Pixel)));
    }
}
