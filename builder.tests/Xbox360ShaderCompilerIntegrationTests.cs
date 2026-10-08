using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderCompilerIntegrationTests {
    const string ShadercLibraryPath = @"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll";
    const string TransformBinarySha256 = "40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF";
    const string TextureBinarySha256 = "D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A";

    [Fact]
    public void Real_shaderc_transform_reflects_actual_vertex_layout_and_stable_binary() {
        Xbox360ShaderBackend backend = CreateRealBackend();
        string sourcePath = Path.Combine(FindRepositoryRoot(), "tests", "shaders", "fixtures", "transform_vs.hlsl");
        string source = File.ReadAllText(sourcePath);
        ShaderCompileRequest request = CreateRequest(source, "fixtures/transform_vs.hlsl", "main", ShaderStage.Vertex);

        Xbox360ShaderCompileResult result = Assert.IsType<Xbox360ShaderCompileResult>(backend.Compile(request, new EmptyIncludeResolver()));

        Assert.True(result.Success);
        Assert.Equal(TransformBinarySha256, Convert.ToHexString(SHA256.HashData(result.Binary.Bytecode)));
        Assert.Empty(result.Diagnostics);
        Assert.Equal(request.ProgramName, result.ProgramDefinition.Name);
        Assert.Equal(request.EntryPoint, result.ProgramDefinition.EntryPoint);
        Assert.Equal(request.Variant, result.Binary.Variant);
        Assert.Equal(new[] { ("LOCATION", 0, "float3"), ("LOCATION", 2, "float2"), ("LOCATION", 3, "float4") },
            result.ProgramDefinition.Inputs.Select(item => (item.Semantic, item.Index, item.Format)));
        Assert.Equal(new[] { ("LOCATION", 0, "float4"), ("LOCATION", 1, "float2"), ("SV_POSITION", 0, "float4") },
            result.ProgramDefinition.Outputs.Select(item => (item.Semantic, item.Index, item.Format)));

        ShaderBinding transform = Assert.Single(result.ProgramDefinition.Bindings);
        Assert.Equal("Transform", transform.Name);
        Assert.Equal(ShaderResourceType.ConstantBuffer, transform.Type);
        Assert.Equal(0, transform.Set);
        Assert.Equal(0, transform.Slot);
        Assert.Equal(80, transform.Size);
        Assert.Collection(transform.Members,
            member => Assert.Equal(("clipFromObject", "float4x4", 0, 64), (member.Name, member.Type, member.Offset, member.Size)),
            member => Assert.Equal(("tint", "float4", 64, 16), (member.Name, member.Type, member.Offset, member.Size)));
        Assert.Equal(64, result.ToolIdentity.Length);
        Assert.Equal(64, result.CompileIdentity.Length);
        Assert.Empty(result.IncludedSources);

        using JsonDocument metadata = JsonDocument.Parse(result.NativeMetadataJson);
        Assert.Equal("vertex", metadata.RootElement.GetProperty("stage").GetString());
        Assert.Equal("main", metadata.RootElement.GetProperty("entrypoint").GetString());
        Assert.Equal("big_endian", metadata.RootElement.GetProperty("binary_dword_byte_order").GetString());

        Xbox360ShaderCompileResult alternateVariant = Assert.IsType<Xbox360ShaderCompileResult>(
            backend.Compile(Clone(request, variant: "alternate"), new EmptyIncludeResolver()));
        Assert.Equal(result.Binary.Bytecode, alternateVariant.Binary.Bytecode);
        Assert.NotEqual(result.CompileIdentity, alternateVariant.CompileIdentity);
        Assert.Equal(result.ToolIdentity, alternateVariant.ToolIdentity);

        Xbox360ShaderCompileResult differentOptions = Assert.IsType<Xbox360ShaderCompileResult>(
            backend.Compile(Clone(request, treatWarningsAsErrors: true), new EmptyIncludeResolver()));
        Assert.NotEqual(result.CompileIdentity, differentOptions.CompileIdentity);
    }

    [Fact]
    public void Real_shaderc_textured_pixel_maps_raw_texture_pair_with_request_shifts() {
        Xbox360ShaderBackend backend = CreateRealBackend();
        string sourcePath = Path.Combine(FindRepositoryRoot(), "tests", "shaders", "fixtures", "texture_ps.hlsl");
        ShaderCompileRequest request = CreateRequest(
            File.ReadAllText(sourcePath), "fixtures/texture_ps.hlsl", "main", ShaderStage.Pixel,
            bindingPolicy: new ShaderBindingPolicy(0, 0, 100, 200, 300));

        Xbox360ShaderCompileResult result = Assert.IsType<Xbox360ShaderCompileResult>(backend.Compile(request, new EmptyIncludeResolver()));

        Assert.Equal(TextureBinarySha256, Convert.ToHexString(SHA256.HashData(result.Binary.Bytecode)));
        ShaderBinding textureBinding = Assert.Single(result.ProgramDefinition.Bindings, item => item.Type == ShaderResourceType.Texture2D);
        Assert.Equal("texture0", textureBinding.Name);
        Assert.Equal(0, textureBinding.Set);
        Assert.Equal(100, textureBinding.Slot);
        ShaderBinding samplerBinding = Assert.Single(result.ProgramDefinition.Bindings, item => item.Type == ShaderResourceType.Sampler);
        Assert.Equal("sampler0", samplerBinding.Name);
        Assert.Equal(0, samplerBinding.Set);
        Assert.Equal(200, samplerBinding.Slot);
        Assert.Equal(new[] { ("LOCATION", 0, "float4"), ("LOCATION", 1, "float2") },
            result.ProgramDefinition.Inputs.Select(item => (item.Semantic, item.Index, item.Format)));
        Assert.Single(result.ProgramDefinition.Outputs);
        Assert.Equal(("LOCATION", 0, "float4"),
            (result.ProgramDefinition.Outputs[0].Semantic, result.ProgramDefinition.Outputs[0].Index, result.ProgramDefinition.Outputs[0].Format));

        using JsonDocument metadata = JsonDocument.Parse(result.NativeMetadataJson);
        JsonElement texture = Assert.Single(metadata.RootElement.GetProperty("textures").EnumerateArray());
        Assert.Equal(0, texture.GetProperty("texture_binding").GetInt32());
        Assert.Equal(0, texture.GetProperty("sampler_binding").GetInt32());
        Assert.Equal(1, texture.GetProperty("fetch_constant").GetInt32());
    }

    [Fact]
    public void Real_shader_service_resolves_conditional_nested_includes_and_recompiles_changed_content() {
        Xbox360ShaderBackend backend = CreateRealBackend();
        string source = """
            #if ENABLED
            #include "outer.hlsli"
            #else
            #include "inactive.hlsli"
            #endif
            struct Input { [[vk::location(0)]] float4 color : COLOR0; };
            float4 PSShade(Input input) : SV_Target0 { return includedColor(input.color); }
            """;
        string firstInner = "cbuffer IncludedTint : register(b0) { float4 tint; };\nfloat4 includedColor(float4 color) { return color * tint; }\n";
        string secondInner = "cbuffer IncludedTint : register(b0) { float4 tint; };\nfloat4 includedColor(float4 color) { return color + tint; }\n";
        var resolver = new NestedIncludeResolver(firstInner);
        var cache = new ThrowingCache();
        var service = new ShaderCompileService(resolver, cache, new ShaderSourceHasher());
        service.RegisterBackend(backend);
        ShaderCompileRequest request = CreateRequest(
            source, "virtual/main.hlsl", "PSShade", ShaderStage.Pixel,
            programName: "include-program", variant: "include-variant",
            defines: [new ShaderDefine("ENABLED", "1")]);

        Xbox360ShaderCompileResult first = Assert.IsType<Xbox360ShaderCompileResult>(service.Compile(request));
        Assert.Equal(new[] { "outer.hlsli", "inner.hlsli" }, resolver.RequestedNames);
        Assert.Equal(new[] { "virtual/outer.hlsli", "virtual/inner.hlsli" }, first.IncludedSources.Select(item => item.SourceName));
        Assert.Equal(Sha256(firstInner), first.IncludedSources[1].Sha256);
        ShaderBinding includedTint = Assert.Single(first.ProgramDefinition.Bindings);
        Assert.Equal("IncludedTint", includedTint.Name);
        Assert.Equal(0, includedTint.Slot);
        Assert.Equal(("tint", "float4", 0, 16),
            (includedTint.Members[0].Name, includedTint.Members[0].Type, includedTint.Members[0].Offset, includedTint.Members[0].Size));
        Assert.Equal("PSShade", first.ProgramDefinition.EntryPoint);
        Assert.Equal("ENABLED=1", Assert.Single(first.ProgramDefinition.Variants).Defines[0]);

        resolver.InnerSource = secondInner;
        resolver.ResetRequests();
        Xbox360ShaderCompileResult second = Assert.IsType<Xbox360ShaderCompileResult>(service.Compile(request));

        Assert.Equal(new[] { "outer.hlsli", "inner.hlsli" }, resolver.RequestedNames);
        Assert.NotSame(first, second);
        Assert.NotEqual(first.Binary.Bytecode, second.Binary.Bytecode);
        Assert.NotEqual(first.CompileIdentity, second.CompileIdentity);
        Assert.Equal(Sha256(secondInner), second.IncludedSources[1].Sha256);
        Assert.Equal(0, cache.LookupCount);
        Assert.Equal(0, cache.StoreCount);

        resolver.InnerSource = "float4 includedColor(float4 color) { return color * ; }\n";
        Xbox360ShaderCompilationException failed = Assert.Throws<Xbox360ShaderCompilationException>(() => service.Compile(request));
        Assert.False(string.IsNullOrWhiteSpace(failed.Diagnostic));
        Assert.Contains("virtual/inner.hlsli", failed.Diagnostic, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(request.Source.Path, failed.SourcePath);
        resolver.InnerSource = secondInner;
        Xbox360ShaderCompileResult recovered = Assert.IsType<Xbox360ShaderCompileResult>(service.Compile(request));
        Assert.NotSame(second, recovered);
        Assert.Equal(second.Binary.Bytecode, recovered.Binary.Bytecode);
        Assert.Equal(0, cache.LookupCount);
        Assert.Equal(0, cache.StoreCount);
    }

    static Xbox360ShaderBackend CreateRealBackend() {
        Assert.True(File.Exists(ShadercLibraryPath), $"Real shaderc integration prerequisite is missing: {ShadercLibraryPath}");
        string builderDirectory = Path.GetDirectoryName(typeof(Xbox360ShaderBackend).Assembly.Location);
        string[] bundledTools = [
            Path.Combine("shader-tools", "xenos_asm.py"),
            Path.Combine("shader-tools", "shader-probe", "backend_protocol.py"),
            Path.Combine("shader-tools", "shader-probe", "backend_layout.py"),
            Path.Combine("shader-tools", "shader-probe", "shader_probe.py"),
            Path.Combine("shader-tools", "shader-probe", "lower_xenos.py"),
            Path.Combine("shader-tools", "shader-probe", "spirv_parser.py"),
            Path.Combine("shader-tools", "shader-probe", "vertex_layout.py")
        ];
        foreach (string relativePath in bundledTools) {
            string path = Path.Combine(builderDirectory, relativePath);
            Assert.True(File.Exists(path), $"Expected the builder-bundled shader tool at '{path}'.");
        }

        return new Xbox360ShaderBackend(new Xbox360ShaderCompilerOptions(
            shadercLibraryPath: ShadercLibraryPath,
            timeout: TimeSpan.FromSeconds(30)));
    }

    static ShaderCompileRequest CreateRequest(
        string source,
        string sourcePath,
        string entryPoint,
        ShaderStage stage,
        string programName = "integration-program",
        string variant = "default",
        IReadOnlyList<ShaderDefine> defines = null,
        ShaderBindingPolicy bindingPolicy = null,
        bool generateDebugInfo = false,
        bool optimize = false,
        bool treatWarningsAsErrors = false) {
        bindingPolicy ??= new ShaderBindingPolicy(0, 0, 100, 200, 300);
        return new ShaderCompileRequest(
            new ShaderSourceInfo(sourcePath, source),
            programName,
            entryPoint,
            stage,
            ShaderCompileTarget.Xbox360,
            new ShaderModel(4, 0),
            variant,
            defines ?? Array.Empty<ShaderDefine>(),
            new ShaderCompileOptions(bindingPolicy, generateDebugInfo, optimize, treatWarningsAsErrors));
    }

    static ShaderCompileRequest Clone(
        ShaderCompileRequest request,
        string variant = null,
        bool? generateDebugInfo = null,
        bool? optimize = null,
        bool? treatWarningsAsErrors = null) => new(
            request.Source,
            request.ProgramName,
            request.EntryPoint,
            request.Stage,
            request.Target,
            request.ShaderModel,
            variant ?? request.Variant,
            request.Defines,
            new ShaderCompileOptions(
                request.Options.BindingPolicy,
                generateDebugInfo ?? request.Options.GenerateDebugInfo,
                optimize ?? request.Options.Optimize,
                treatWarningsAsErrors ?? request.Options.TreatWarningsAsErrors));

    static string FindRepositoryRoot() {
        for (DirectoryInfo directory = new(AppContext.BaseDirectory); directory != null; directory = directory.Parent) {
            if (File.Exists(Path.Combine(directory.FullName, "tests", "shaders", "fixtures", "transform_vs.hlsl"))) {
                return directory.FullName;
            }
        }
        throw new DirectoryNotFoundException("Could not locate the checkout containing tests/shaders/fixtures.");
    }

    static string Sha256(string value) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value))).ToLowerInvariant();

    sealed class EmptyIncludeResolver : IShaderIncludeResolver {
        public ShaderIncludeResult Resolve(string requestingFile, string includePath) =>
            throw new InvalidOperationException($"Unexpected include '{includePath}' from '{requestingFile}'.");
    }

    sealed class NestedIncludeResolver : IShaderIncludeResolver {
        readonly List<string> Requests = new();

        public NestedIncludeResolver(string innerSource) {
            InnerSource = innerSource;
        }

        public string InnerSource { get; set; }
        public IReadOnlyList<string> RequestedNames => Requests;

        public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
            Requests.Add(includePath);
            return includePath switch {
                "outer.hlsli" => new ShaderIncludeResult("virtual/outer.hlsli", "#include \"inner.hlsli\"\n"),
                "inner.hlsli" => new ShaderIncludeResult("virtual/inner.hlsli", InnerSource),
                _ => throw new FileNotFoundException($"Unexpected active include '{includePath}' requested by '{requestingFile}'.")
            };
        }

        public void ResetRequests() => Requests.Clear();
    }

    sealed class ThrowingCache : IShaderCompileCache {
        public int LookupCount { get; private set; }
        public int StoreCount { get; private set; }

        public bool TryGet(ShaderCompileCacheKey key, out ShaderCompileResult result) {
            LookupCount++;
            result = null;
            throw new InvalidOperationException("Xbox 360 shader service must bypass the shared cache.");
        }

        public void Store(ShaderCompileCacheKey key, ShaderCompileResult result) {
            StoreCount++;
            throw new InvalidOperationException("Xbox 360 shader service must bypass the shared cache.");
        }
    }
}
