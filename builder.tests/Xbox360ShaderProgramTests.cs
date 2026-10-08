using System.Security.Cryptography;
using System.Collections.Generic;
using System.Text.Json.Nodes;
using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderProgramTests {
    [Fact]
    public void FromCompilation_snapshots_the_real_transform_program() {
        Xbox360ShaderCompileResult compilation = Xbox360ShaderProgramTestSupport.TransformVertex;

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(compilation);

        Assert.Equal("source/transform_vs.hlsl", program.SourcePath);
        Assert.Equal("arbitrary-vertex-program", program.ProgramName);
        Assert.Equal("main", program.EntryPoint);
        Assert.Equal("release-candidate", program.Variant);
        Assert.Equal(ShaderStage.Vertex, program.Stage);
        Assert.Equal(51, program.MicrocodeWords.Count);
        Assert.Equal(9, program.RegisterCount);
        Assert.Equal(8, program.RegisterField);
        Assert.Equal("40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF", program.BytecodeSha256);
        Assert.Equal(program.BytecodeSha256, Convert.ToHexString(SHA256.HashData(compilation.Binary.Bytecode)));
        Assert.Equal(compilation.ToolIdentity, program.ToolIdentity);
        Assert.Equal(compilation.CompileIdentity, program.CompileIdentity);
        Assert.Equal(new[] { (0, 3, 0, 12, 0, "32_32_32_FLOAT"), (2, 2, 0, 12, 6, "32_32_FLOAT"), (3, 4, 0, 12, 8, "32_32_32_32_FLOAT") },
            program.Inputs.Select(input => (input.Index, input.Width, input.VertexFetch.FetchConstant,
                input.VertexFetch.Stride, input.VertexFetch.Offset, input.VertexFetch.HardwareFormat)));
        Assert.Collection(program.Outputs,
            output => Assert.Equal((Xbox360ShaderInterfaceKind.Location, 0, 4, 0),
                (output.Kind, output.Index, output.Width, output.PhysicalSlot)),
            output => Assert.Equal((Xbox360ShaderInterfaceKind.Location, 1, 2, 1),
                (output.Kind, output.Index, output.Width, output.PhysicalSlot)),
            output => Assert.Equal((Xbox360ShaderInterfaceKind.Position, 0, 4, null),
                (output.Kind, output.Index, output.Width, output.PhysicalSlot)));

        Xbox360ShaderResource constantBuffer = Assert.Single(program.Resources,
            resource => resource.Type == ShaderResourceType.ConstantBuffer);
        Assert.Equal(17, constantBuffer.LogicalSlot);
        Assert.Equal(0, constantBuffer.PhysicalBinding);
        Assert.Equal(80, program.ConstantBuffer.SizeBytes);
        Assert.Equal(5, program.ConstantBuffer.RegisterCapacity);
        Assert.Equal(0, program.ConstantBuffer.PhysicalBankBaseRegister);
        Assert.Equal(new[] { "clipFromObject", "tint" }, program.ConstantBuffer.DeclaredMembers.Select(member => member.Name));
        Assert.Collection(program.ConstantBuffer.UsedMembers,
            member => {
                Assert.Equal(("clipFromObject", 0, "row_major", 16),
                    (member.Name, member.OffsetBytes, member.MatrixMajor, member.MatrixStrideBytes));
                Assert.Equal(new[] { 0, 1, 2, 3 }, member.RegisterIndices);
            },
            member => {
                Assert.Equal(("tint", 64, null, null),
                    (member.Name, member.OffsetBytes, member.MatrixMajor, member.MatrixStrideBytes));
                Assert.Equal(new[] { 4 }, member.RegisterIndices);
            });
    }

    [Fact]
    public void FromCompilation_snapshots_the_real_textured_pixel_program() {
        Xbox360ShaderCompileResult compilation = Xbox360ShaderProgramTestSupport.TexturedPixel;

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(compilation);

        Assert.Equal("source/texture_ps.hlsl", program.SourcePath);
        Assert.Equal("arbitrary-pixel-program", program.ProgramName);
        Assert.Equal("main", program.EntryPoint);
        Assert.Equal("release-candidate", program.Variant);
        Assert.Equal(ShaderStage.Pixel, program.Stage);
        Assert.Equal(21, program.MicrocodeWords.Count);
        Assert.Equal(5, program.RegisterCount);
        Assert.Equal(4, program.RegisterField);
        Assert.Equal("D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A", program.BytecodeSha256);
        Assert.Equal(program.BytecodeSha256, Convert.ToHexString(SHA256.HashData(compilation.Binary.Bytecode)));
        Assert.Null(program.ConstantBuffer);
        Assert.Equal(new[] { (ShaderResourceType.Texture2D, 101, 0, (int?)1), (ShaderResourceType.Sampler, 202, 0, (int?)1) },
            program.Resources.OrderBy(resource => resource.Type).Select(resource =>
                (resource.Type, resource.LogicalSlot, resource.PhysicalBinding, resource.PhysicalSlot)));
        Assert.Equal(new[] { (0, 4, (int?)0), (1, 2, (int?)1) }, program.Inputs.Select(input =>
            (input.Index, input.Width, input.PhysicalSlot)));
        Assert.Single(program.Outputs);
        Assert.Equal((Xbox360ShaderInterfaceKind.Location, 0, 4),
            (program.Outputs[0].Kind, program.Outputs[0].Index, program.Outputs[0].Width));
    }

    [Fact]
    public void FromCompilation_returns_read_only_snapshots_detached_from_mutable_compiler_arrays() {
        Xbox360ShaderCompileResult compilation = Xbox360ShaderProgramTestSupport.CloneResult(
            Xbox360ShaderProgramTestSupport.TransformVertex);
        byte firstByte = compilation.Binary.Bytecode[0];

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(compilation);

        uint firstWord = program.MicrocodeWords[0];
        compilation.Binary.Bytecode[0] ^= 0xff;
        compilation.ProgramDefinition.Inputs[0] = new ShaderVertexElement("LOCATION", 7, "float4");
        compilation.ProgramDefinition.Bindings[0].Members[0] = new ShaderConstantMember("changed", "float", 0, 4);
        Assert.Equal(firstWord, program.MicrocodeWords[0]);
        Assert.Equal((byte)(firstByte ^ 0xff), compilation.Binary.Bytecode[0]);
        Assert.Equal(0, program.Inputs[0].Index);
        Assert.Equal("clipFromObject", program.ConstantBuffer.DeclaredMembers[0].Name);

        IList<uint> words = Assert.IsAssignableFrom<IList<uint>>(program.MicrocodeWords);
        Assert.True(words.IsReadOnly);
        Action mutation = () => words[0] = 0;
        Assert.Throws<NotSupportedException>(mutation);
    }

    [Fact]
    public void FromCompilation_rejects_duplicate_json_keys() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string duplicateStage = source.NativeMetadataJson.Insert(
            source.NativeMetadataJson.IndexOf('{') + 1, "\"stage\":\"vertex\",");
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: duplicateStage);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_metadata_larger_than_eight_mebibytes() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        Xbox360ShaderCompileResult oversized = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: new string(' ', 8 * 1024 * 1024 + 1));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(oversized);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Theory]
    [InlineData("register_count", 0)]
    [InlineData("register_count", 65)]
    [InlineData("highest_register_index", 7)]
    [InlineData("sq_program_cntl_register_field", 64)]
    public void FromCompilation_rejects_invalid_register_metadata(string key, int value) {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => root[key] = value);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Theory]
    [InlineData("stage", "pixel")]
    [InlineData("entrypoint", "wrong_entry")]
    [InlineData("binary_dword_byte_order", "little_endian")]
    public void FromCompilation_rejects_metadata_that_disagrees_with_the_request(string key, string value) {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => root[key] = value);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_unknown_native_features() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            root["unsupported_features"] = new JsonArray(JsonValue.Create("geometry"));
        });
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_required_metadata_field_with_the_wrong_json_type() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => root["register_count"] = "9");
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_aligned_microcode_whose_dword_count_is_not_instruction_triples() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        byte[] truncated = source.Binary.Bytecode[..^4];
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, binary: Xbox360ShaderProgramTestSupport.CloneBinary(source.Binary, bytecode: truncated));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_request_binary_and_reflection_disagreement() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderProgramDefinition wrongStage = Xbox360ShaderProgramTestSupport.CloneDefinition(
            source.ProgramDefinition, stage: ShaderStage.Pixel);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, programDefinition: wrongStage);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_invalid_resource_and_physical_input_mappings() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            root["input_map"]["0"]["fetch_constant"] = 1;
        });
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_duplicate_and_out_of_range_reflected_interfaces() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderVertexElement[] duplicateInputs = source.ProgramDefinition.Inputs
            .Select(element => new ShaderVertexElement(element.Semantic, element.Index, element.Format))
            .Append(new ShaderVertexElement("LOCATION", 0, "float3"))
            .ToArray();
        Xbox360ShaderCompileResult duplicate = Xbox360ShaderProgramTestSupport.CloneResult(
            source, programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, inputs: duplicateInputs));
        Action createDuplicate = () => Xbox360ShaderProgramFactory.FromCompilation(duplicate);
        Assert.Throws<InvalidDataException>(createDuplicate);

        ShaderVertexElement[] outOfRangeOutputs = source.ProgramDefinition.Outputs
            .Select(element => new ShaderVertexElement(element.Semantic, element.Index, element.Format)).ToArray();
        outOfRangeOutputs[0] = new ShaderVertexElement("LOCATION", 8, "float4");
        Xbox360ShaderCompileResult outOfRange = Xbox360ShaderProgramTestSupport.CloneResult(
            source, programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, outputs: outOfRangeOutputs));
        Action createOutOfRange = () => Xbox360ShaderProgramFactory.FromCompilation(outOfRange);
        Assert.Throws<InvalidDataException>(createOutOfRange);
    }

    [Fact]
    public void FromCompilation_keeps_declared_constants_separate_from_actual_used_accesses() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            JsonArray used = (JsonArray)root["uniforms"]["0"]["members"];
            used.RemoveAt(0);
        });
        Xbox360ShaderCompileResult compilation = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(compilation);

        Assert.Equal(new[] { "clipFromObject", "tint" }, program.ConstantBuffer.DeclaredMembers.Select(member => member.Name));
        Assert.Equal(new[] { "tint" }, program.ConstantBuffer.UsedMembers.Select(member => member.Name));
        Assert.Equal("float4x4", program.ConstantBuffer.DeclaredMembers[0].Type);
    }

    [Fact]
    public void FromCompilation_rejects_native_used_constant_with_wrong_matrix_stride() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            root["uniforms"]["0"]["members"][0]["matrix_stride_bytes"] = 8;
        });
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_reflected_constant_member_with_invalid_type_layout() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderBinding original = source.ProgramDefinition.Bindings[0];
        ShaderBinding invalid = new(original.Name, original.Type, original.Set, original.Slot,
            original.Size, [new ShaderConstantMember("bad", "float4x4", 4, 64)]);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, bindings: [invalid]));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_missing_native_uniform_metadata_for_a_declared_buffer() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root =>
            root["uniforms"] = new JsonObject());
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_mismatched_native_texture_pair() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TexturedPixel;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root =>
            root["textures"][0]["fetch_constant"] = 2);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_request_for_another_target() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderCompileTarget unsupportedTarget = (ShaderCompileTarget)77;
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            request: Xbox360ShaderProgramTestSupport.CloneRequest(source.Request, target: unsupportedTarget),
            binary: Xbox360ShaderProgramTestSupport.CloneBinary(source.Binary, target: unsupportedTarget));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_request_outside_shader_model_four_zero() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            request: Xbox360ShaderProgramTestSupport.CloneRequest(source.Request, shaderModel: new ShaderModel(4, 1)));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Theory]
    [InlineData("tool")]
    [InlineData("000000000000000000000000000000000000000000000000000000000000000g")]
    public void FromCompilation_rejects_an_invalid_tool_identity(string identity) {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, toolIdentity: identity);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_accepts_register_sixty_four_and_encoded_field_sixty_three() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            root["register_count"] = 64;
            root["highest_register_index"] = 63;
            root["sq_program_cntl_register_field"] = 63;
        });

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.CloneResult(source, nativeMetadataJson: metadata));

        Assert.Equal(64, program.RegisterCount);
        Assert.Equal(63, program.RegisterField);
    }

    [Fact]
    public void FromCompilation_preserves_binding_indices_above_255_and_keeps_texture_fetch_at_tf1() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TexturedPixel;
        ShaderBinding[] bindings = [
            new ShaderBinding("image_with_unrelated_name", ShaderResourceType.Texture2D, 0, 401, 0, []),
            new ShaderBinding("state_with_another_name", ShaderResourceType.Sampler, 0, 502, 0, [])
        ];
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            root["textures"][0]["texture_binding"] = 300;
            root["textures"][0]["sampler_binding"] = 300;
        });
        ShaderProgramDefinition definition = Xbox360ShaderProgramTestSupport.CloneDefinition(
            source.ProgramDefinition, bindings: bindings);

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.CloneResult(source, programDefinition: definition, nativeMetadataJson: metadata));

        Xbox360ShaderResource textureResource = Assert.Single(program.Resources,
            resource => resource.Type == ShaderResourceType.Texture2D);
        Xbox360ShaderResource samplerResource = Assert.Single(program.Resources,
            resource => resource.Type == ShaderResourceType.Sampler);
        Assert.Equal(("image_with_unrelated_name", 401, 300, 1),
            (textureResource.Name, textureResource.LogicalSlot, textureResource.PhysicalBinding, textureResource.PhysicalSlot));
        Assert.Equal(("state_with_another_name", 502, 300, 1),
            (samplerResource.Name, samplerResource.LogicalSlot, samplerResource.PhysicalBinding, samplerResource.PhysicalSlot));
    }

    [Fact]
    public void FromCompilation_preserves_constant_offsets_above_255_as_vec4_register_indices() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderBinding shiftedBuffer = new("unrelated_constant_name", ShaderResourceType.ConstantBuffer,
            0, 17, 272, [new ShaderConstantMember("wide_offset_value", "float4", 256, 16)]);
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            JsonObject uniform = (JsonObject)root["uniforms"]["0"];
            uniform["register_count"] = 17;
            JsonArray members = (JsonArray)uniform["members"];
            members.Clear();
            members.Add(new JsonObject {
                ["offset_bytes"] = 256,
                ["registers"] = new JsonArray(JsonValue.Create(16))
            });
        });
        ShaderProgramDefinition definition = Xbox360ShaderProgramTestSupport.CloneDefinition(
            source.ProgramDefinition, bindings: [shiftedBuffer]);

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.CloneResult(source, programDefinition: definition, nativeMetadataJson: metadata));

        Assert.Equal(272, program.ConstantBuffer.SizeBytes);
        Assert.Equal(256, program.ConstantBuffer.DeclaredMembers[0].OffsetBytes);
        Assert.Equal(new[] { 16 }, program.ConstantBuffer.UsedMembers[0].RegisterIndices);
        Assert.Equal(0, program.ConstantBuffer.PhysicalBankBaseRegister);
        Assert.Equal("unrelated_constant_name", Assert.Single(program.Resources).Name);
    }

    [Fact]
    public void FromCompilation_uses_the_pixel_constant_bank_base_for_a_real_constant_buffer() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.UniformPixel;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            root["register_count"] = 1;
            root["highest_register_index"] = 0;
            root["sq_program_cntl_register_field"] = 0;
        });
        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.CloneResult(source, nativeMetadataJson: metadata));

        Assert.Equal(256, program.ConstantBuffer.PhysicalBankBaseRegister);
        Assert.Equal(1, program.ConstantBuffer.RegisterCapacity);
    }

    [Fact]
    public void FromCompilation_rejects_an_unknown_reflected_resource_type() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderBinding[] bindings = [new ShaderBinding(
            "not_a_supported_resource", (ShaderResourceType)999, 0, 0, 0, [])];
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, bindings: bindings));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_texture_without_its_sampler() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TexturedPixel;
        ShaderBinding[] bindings = source.ProgramDefinition.Bindings
            .Where(binding => binding.Type != ShaderResourceType.Sampler)
            .Select(binding => new ShaderBinding(binding.Name, binding.Type, binding.Set, binding.Slot,
                binding.Size, binding.Members.Select(member => new ShaderConstantMember(
                    member.Name, member.Type, member.Offset, member.Size)).ToArray())).ToArray();
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, bindings: bindings));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_pixel_output_that_is_not_float4_location_zero() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TexturedPixel;
        ShaderVertexElement[] outputs = [new ShaderVertexElement("LOCATION", 0, "float3")];
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, outputs: outputs));

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_value_based_native_uniform_metadata() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root =>
            root["uniforms"]["0"]["register_values"] = new JsonArray());
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_noncontiguous_native_used_register_range() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root =>
            root["uniforms"]["0"]["members"][0]["registers"][1] = 3);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_matrix_decorations_on_a_scalar_or_vector_access() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root =>
            root["uniforms"]["0"]["members"][1]["major"] = "row_major");
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_native_inputs_that_disagree_with_the_pixel_interpolator_map() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TexturedPixel;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root =>
            root["inputs"]["0"]["register"] = 1);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_duplicate_native_vertex_varying_export() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            JsonArray outputs = (JsonArray)root["outputs"];
            outputs.Add(outputs[1].DeepClone());
        });
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }

    [Fact]
    public void FromCompilation_rejects_a_duplicate_native_pixel_color_export() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TexturedPixel;
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            JsonArray outputs = (JsonArray)root["outputs"];
            outputs.Add(outputs[0].DeepClone());
        });
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadata);

        Action createProgram = () => Xbox360ShaderProgramFactory.FromCompilation(malformed);

        Assert.Throws<InvalidDataException>(createProgram);
    }
}
