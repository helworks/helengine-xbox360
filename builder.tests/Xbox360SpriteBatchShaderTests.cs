using System.Text.Json;
using System.Text.Json.Nodes;
using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360SpriteBatchShaderTests {
    const string ShadercLibraryPath = @"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll";
    const string SourcePath = @"C:\dev\helworks\helengine\engine\helengine.editor\shaders\builtin\SpriteBatchShader.hlsl";
    const string VertexSha256 = "9CD3526ECD7A2016A89CABB10B1388979C58A6F62D24785D0E4206234B32882E";
    const string PixelSha256 = "7C67AD650636CD7FF621C85568792DED67A3F2120A0DD3975FA78B60B22E849C";

    [Theory]
    [InlineData("fetch_constant")]
    [InlineData("stride")]
    [InlineData("offset")]
    [InlineData("format")]
    public void Batch2d_factory_rejects_native_fetches_that_disagree_with_the_selected_profile(string field) {
        Xbox360ShaderCompileResult source = CompileSharedVertex(optimize: false);
        string metadataJson = EditVertexFetch(source.NativeMetadataJson, field);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source, nativeMetadataJson: metadataJson);

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderProgramFactory.FromCompilation(malformed));
    }

    [Fact]
    public void Batch2d_factory_rejects_a_reflected_input_width_outside_the_selected_fetch_contract() {
        Xbox360ShaderCompileResult source = CompileSharedVertex(optimize: false);
        ShaderVertexElement[] inputs = source.ProgramDefinition.Inputs
            .Select(input => new ShaderVertexElement(input.Semantic, input.Index, input.Format)).ToArray();
        inputs[0] = new ShaderVertexElement(inputs[0].Semantic, inputs[0].Index, "float3");
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, inputs: inputs));

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderProgramFactory.FromCompilation(malformed));
    }

    [Fact]
    public void Batch2d_factory_rejects_an_out_of_range_vertex_fetch_location() {
        Xbox360ShaderCompileResult source = CompileSharedVertex(optimize: false);
        JsonObject metadata = JsonNode.Parse(source.NativeMetadataJson).AsObject();
        JsonObject nativeInput = (JsonObject)metadata["inputs"]["0"].DeepClone();
        JsonObject inputMap = (JsonObject)metadata["input_map"]["0"].DeepClone();
        metadata["inputs"].AsObject().Remove("0");
        metadata["input_map"].AsObject().Remove("0");
        metadata["inputs"]["6"] = nativeInput;
        metadata["input_map"]["6"] = inputMap;
        ShaderVertexElement[] inputs = source.ProgramDefinition.Inputs
            .Select(input => new ShaderVertexElement(input.Semantic, input.Index, input.Format)).ToArray();
        inputs[0] = new ShaderVertexElement("LOCATION", 6, inputs[0].Format);
        Xbox360ShaderCompileResult malformed = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, inputs: inputs),
            nativeMetadataJson: metadata.ToJsonString());

        Assert.Throws<InvalidDataException>(() => Xbox360ShaderProgramFactory.FromCompilation(malformed));
    }

    [Fact]
    public void Batch2d_factory_accepts_the_complete_six_location_fetch_contract() {
        Xbox360ShaderCompileResult source = CompileSharedVertex(optimize: false);
        JsonObject metadata = JsonNode.Parse(source.NativeMetadataJson).AsObject();
        List<ShaderVertexElement> inputs = source.ProgramDefinition.Inputs
            .Select(input => new ShaderVertexElement(input.Semantic, input.Index, input.Format)).ToList();
        for (int location = 3; location < 6; location++) {
            JsonObject nativeInput = (JsonObject)metadata["inputs"]["0"].DeepClone();
            nativeInput["fetch_constant"] = 0;
            nativeInput["stride"] = 24;
            nativeInput["offset"] = location * 4;
            nativeInput["format"] = "32_32_32_32_FLOAT";
            JsonObject inputMap = (JsonObject)nativeInput.DeepClone();
            metadata["inputs"][location.ToString()] = nativeInput;
            metadata["input_map"][location.ToString()] = inputMap;
            inputs.Add(new ShaderVertexElement("LOCATION", location, "float4"));
        }
        Xbox360ShaderCompileResult complete = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(
                source.ProgramDefinition, inputs: inputs.ToArray()),
            nativeMetadataJson: metadata.ToJsonString());

        Xbox360ShaderProgram program = Xbox360ShaderProgramFactory.FromCompilation(complete);

        Assert.Equal(new[] { (0, 0), (1, 4), (2, 8), (3, 12), (4, 16), (5, 20) },
            program.Inputs.Select(input => (input.Index, input.VertexFetch.Offset)));
        Assert.All(program.Inputs, input => {
            Assert.Equal(4, input.Width);
            Assert.Equal(0, input.VertexFetch.FetchConstant);
            Assert.Equal(24, input.VertexFetch.Stride);
            Assert.Equal("32_32_32_32_FLOAT", input.VertexFetch.HardwareFormat);
        });
    }

    [Fact]
    public void Pixel_compile_identity_includes_the_selected_vertex_profile_even_when_binary_matches() {
        StandardShaderVariant variant = Assert.Single(SpriteBatchShaderVariants.All, item => item.Name == "Textured");
        string source = File.ReadAllText(SourcePath);
        ShaderBindingPolicy bindingPolicy = new(0, 0, 0, 0, 0);
        Xbox360ShaderCompileResult meshPixel = Compile(
            CreateBackend(Xbox360ShaderVertexLayout.Mesh3D), source, variant, ShaderStage.Pixel,
            variant.PixelEntryPoint, false, bindingPolicy);
        Xbox360ShaderCompileResult batchPixel = Compile(
            CreateBackend(Xbox360ShaderVertexLayout.Batch2D), source, variant, ShaderStage.Pixel,
            variant.PixelEntryPoint, false, bindingPolicy);

        Assert.Equal(meshPixel.Binary.Bytecode, batchPixel.Binary.Bytecode);
        Assert.NotEqual(meshPixel.CompileIdentity, batchPixel.CompileIdentity);
        Assert.Equal(Xbox360ShaderVertexLayout.Mesh3D, meshPixel.VertexLayout);
        Assert.Equal(Xbox360ShaderVertexLayout.Batch2D, batchPixel.VertexLayout);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Shared_textured_sprite_batch_compiles_as_an_exact_batch2d_pair(bool optimize) {
        Assert.True(File.Exists(ShadercLibraryPath), $"Real shaderc integration prerequisite is missing: {ShadercLibraryPath}");
        Assert.True(File.Exists(SourcePath), $"Shared SpriteBatch shader prerequisite is missing: {SourcePath}");
        string source = File.ReadAllText(SourcePath);
        StandardShaderVariant variant = Assert.Single(SpriteBatchShaderVariants.All, item => item.Name == "Textured");
        Assert.Equal("VS", variant.VertexEntryPoint);
        Assert.Equal("PS", variant.PixelEntryPoint);
        Assert.Empty(variant.Defines);

        Xbox360ShaderBackend backend = CreateBackend(Xbox360ShaderVertexLayout.Batch2D);
        ShaderBindingPolicy bindingPolicy = new(0, 0, 0, 0, 0);
        Xbox360ShaderCompileResult vertexCompilation = Compile(backend, source, variant, ShaderStage.Vertex,
            variant.VertexEntryPoint, optimize, bindingPolicy);
        Xbox360ShaderCompileResult pixelCompilation = Compile(backend, source, variant, ShaderStage.Pixel,
            variant.PixelEntryPoint, optimize, bindingPolicy);

        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(vertexCompilation);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(pixelCompilation);
        Xbox360ShaderProgramPair pair = Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Equal(Xbox360ShaderVertexLayout.Batch2D, vertex.VertexLayout);
        Assert.Equal(Xbox360ShaderVertexLayout.Batch2D, pixel.VertexLayout);
        Assert.Equal(VertexSha256, vertex.BytecodeSha256);
        Assert.Equal(PixelSha256, pixel.BytecodeSha256);
        Assert.Equal(45, vertex.WordCount);
        Assert.Equal(15, pixel.WordCount);
        Assert.Equal(5, vertex.RegisterCount);
        Assert.Equal(4, pixel.RegisterCount);
        Assert.Equal(64, vertex.ConstantBuffer.SizeBytes);
        Assert.Equal(4, vertex.ConstantBuffer.RegisterCapacity);
        Assert.Null(pixel.ConstantBuffer);
        Assert.Equal(new[] { (0, 4, 0, 24, 0), (1, 4, 0, 24, 4), (2, 4, 0, 24, 8) },
            vertex.Inputs.Select(input => (input.Index, input.Width, input.VertexFetch.FetchConstant,
                input.VertexFetch.Stride, input.VertexFetch.Offset)));
        Assert.Equal(new[] { (0, 2, (int?)0), (1, 4, (int?)1) },
            vertex.Outputs.Where(output => output.Kind == Xbox360ShaderInterfaceKind.Location)
                .Select(output => (output.Index, output.Width, output.PhysicalSlot)));
        Assert.Equal(new[] { (0, 2, (int?)0), (1, 4, (int?)1) },
            pixel.Inputs.Select(input => (input.Index, input.Width, input.PhysicalSlot)));
        Assert.Equal(0x00100304u, pair.ProgramControl);
        Assert.Equal(66, pair.ProgramUploadDwords);
        Assert.Equal(18, pair.ConstantUploadDwords);
        Assert.Equal(10, pair.StateUploadDwords);
        Assert.Equal(94, pair.PairUploadDwords);

        AssertNativeVertexLayout(vertexCompilation.NativeMetadataJson);
        AssertNativeTexturePair(pixelCompilation.NativeMetadataJson);
    }

    static Xbox360ShaderCompileResult Compile(
        Xbox360ShaderBackend backend,
        string source,
        StandardShaderVariant variant,
        ShaderStage stage,
        string entryPoint,
        bool optimize,
        ShaderBindingPolicy bindingPolicy) {
        ShaderDefine[] defines = variant.Defines.Select(value => {
            int separator = value.IndexOf('=');
            return separator < 0
                ? new ShaderDefine(value, "1")
                : new ShaderDefine(value[..separator], value[(separator + 1)..]);
        }).ToArray();
        ShaderCompileRequest request = new(
            new ShaderSourceInfo(SourcePath, source),
            stage == ShaderStage.Vertex ? "SpriteBatchShader.vs" : "SpriteBatchShader.ps",
            entryPoint,
            stage,
            ShaderCompileTarget.Xbox360,
            new ShaderModel(4, 0),
            variant.Name,
            defines,
            new ShaderCompileOptions(bindingPolicy, false, optimize, false));
        return Assert.IsType<Xbox360ShaderCompileResult>(backend.Compile(request, new EmptyIncludeResolver()));
    }

    static Xbox360ShaderBackend CreateBackend(Xbox360ShaderVertexLayout vertexLayout) =>
        new(new Xbox360ShaderCompilerOptions(
            shadercLibraryPath: ShadercLibraryPath,
            timeout: TimeSpan.FromSeconds(30),
            vertexLayout: vertexLayout));

    static Xbox360ShaderCompileResult CompileSharedVertex(bool optimize) {
        string source = File.ReadAllText(SourcePath);
        StandardShaderVariant variant = Assert.Single(SpriteBatchShaderVariants.All, item => item.Name == "Textured");
        return Compile(CreateBackend(Xbox360ShaderVertexLayout.Batch2D), source, variant,
            ShaderStage.Vertex, variant.VertexEntryPoint, optimize,
            new ShaderBindingPolicy(0, 0, 0, 0, 0));
    }

    static string EditVertexFetch(string metadataJson, string field) {
        JsonObject metadata = JsonNode.Parse(metadataJson).AsObject();
        JsonNode input = metadata["inputs"]["0"];
        JsonNode inputMap = metadata["input_map"]["0"];
        switch (field) {
            case "fetch_constant":
                input["fetch_constant"] = 1;
                inputMap["fetch_constant"] = 1;
                break;
            case "stride":
                input["stride"] = 12;
                inputMap["stride"] = 12;
                break;
            case "offset":
                input["offset"] = 1;
                inputMap["offset"] = 1;
                break;
            case "format":
                input["format"] = "32_32_32_FLOAT";
                inputMap["format"] = "32_32_32_FLOAT";
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(field));
        }
        return metadata.ToJsonString();
    }

    static void AssertNativeVertexLayout(string metadataJson) {
        using JsonDocument document = JsonDocument.Parse(metadataJson);
        JsonElement metadata = document.RootElement;
        Assert.Equal("batch2d-v1", metadata.GetProperty("vertex_layout").GetString());
        JsonElement inputs = metadata.GetProperty("inputs");
        Assert.Equal(3, inputs.EnumerateObject().Count());
        for (int location = 0; location < 3; location++) {
            Assert.True(inputs.TryGetProperty(location.ToString(), out JsonElement input));
            Assert.Equal(0, input.GetProperty("fetch_constant").GetInt32());
            Assert.Equal(24, input.GetProperty("stride").GetInt32());
            Assert.Equal(location * 4, input.GetProperty("offset").GetInt32());
            Assert.Equal("32_32_32_32_FLOAT", input.GetProperty("format").GetString());
        }
    }

    static void AssertNativeTexturePair(string metadataJson) {
        using JsonDocument document = JsonDocument.Parse(metadataJson);
        JsonElement metadata = document.RootElement;
        Assert.Equal("batch2d-v1", metadata.GetProperty("vertex_layout").GetString());
        Assert.Empty(metadata.GetProperty("uniforms").EnumerateObject());
        JsonElement texture = Assert.Single(metadata.GetProperty("textures").EnumerateArray());
        Assert.Equal(1, texture.GetProperty("fetch_constant").GetInt32());
        Assert.Equal("pixel", texture.GetProperty("stage").GetString());
        Assert.Equal("2D", texture.GetProperty("dimension").GetString());
    }

    sealed class EmptyIncludeResolver : IShaderIncludeResolver {
        public ShaderIncludeResult Resolve(string requestingFile, string includePath) =>
            throw new InvalidOperationException($"Unexpected include '{includePath}' from '{requestingFile}'.");
    }
}
