using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderBackendTests {
    [Fact]
    public void Backend_advertises_only_its_compiled_shader_subset_and_bypasses_shared_cache() {
        var backend = new Xbox360ShaderBackend();

        Assert.Equal(ShaderCompileTarget.Xbox360, backend.Target);
        Assert.Equal(4, backend.Capabilities.MinimumShaderModel.Major);
        Assert.Equal(0, backend.Capabilities.MinimumShaderModel.Minor);
        Assert.Equal(4, backend.Capabilities.MaximumShaderModel.Major);
        Assert.Equal(0, backend.Capabilities.MaximumShaderModel.Minor);
        Assert.Equal([ShaderStage.Vertex, ShaderStage.Pixel], backend.Capabilities.SupportedStages);
        Assert.False(backend.Capabilities.SupportsRayTracing);
        Assert.False(Assert.IsAssignableFrom<IShaderBackendCachePolicy>(backend).UseSharedCache);
    }

    [Fact]
    public void Registry_contribution_is_lazy_and_does_not_make_builder_a_target_provider() {
        var options = new Xbox360ShaderCompilerOptions(
            pythonExecutable: "missing-python-for-lazy-registration",
            protocolScriptPath: Path.Combine(Path.GetTempPath(), "missing-x360-protocol.py"),
            shadercLibraryPath: Path.Combine(Path.GetTempPath(), "missing-x360-shaderc.dll"));
        var backend = new Xbox360ShaderBackend(options);
        var registry = new ShaderBackendRegistry();
        registry.Register(backend);

        Assert.True(registry.ContainsTarget(ShaderCompileTarget.Xbox360));

        var builder = new Xbox360PlatformAssetBuilder();
        Assert.IsAssignableFrom<IShaderBackendRegistryContributor>(builder);
        var contributedRegistry = new ShaderBackendRegistry();
        ((IShaderBackendRegistryContributor)builder).RegisterShaderBackends(contributedRegistry);
        Assert.True(contributedRegistry.ContainsTarget(ShaderCompileTarget.Xbox360));
        Assert.DoesNotContain(
            typeof(Xbox360PlatformAssetBuilder).GetInterfaces(),
            interfaceType => interfaceType.Name == "IShaderCompileTargetProvider");
    }

    [Theory]
    [InlineData(ShaderCompileTarget.DirectX11, ShaderStage.Vertex, 4, 0, "target")]
    [InlineData(ShaderCompileTarget.Xbox360, ShaderStage.Compute, 4, 0, "stage")]
    [InlineData(ShaderCompileTarget.Xbox360, ShaderStage.Vertex, 5, 0, "model")]
    public void Unsupported_request_contracts_fail_with_request_context_before_tool_launch(
        ShaderCompileTarget target,
        ShaderStage stage,
        int modelMajor,
        int modelMinor,
        string diagnosticPart) {
        var backend = new Xbox360ShaderBackend(new Xbox360ShaderCompilerOptions(
            pythonExecutable: "missing-python-for-preflight",
            protocolScriptPath: Path.Combine(Path.GetTempPath(), "missing-x360-protocol.py"),
            shadercLibraryPath: Path.Combine(Path.GetTempPath(), "missing-x360-shaderc.dll")));
        ShaderCompileRequest request = CreateRequest(target: target, stage: stage, model: new ShaderModel(modelMajor, modelMinor));

        Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(
            () => backend.Compile(request, new EmptyIncludeResolver()));

        Assert.Contains(diagnosticPart, exception.Diagnostic, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(request.Source.Path, exception.SourcePath);
        Assert.Equal(request.ProgramName, exception.ProgramName);
        Assert.Equal(request.EntryPoint, exception.EntryPoint);
        Assert.Equal(request.Stage, exception.Stage);
        Assert.Equal(request.Variant, exception.Variant);
    }

    [Fact]
    public void Nonzero_default_space_is_rejected_before_tool_launch() {
        var backend = new Xbox360ShaderBackend(new Xbox360ShaderCompilerOptions(
            pythonExecutable: "missing-python-for-policy-check",
            protocolScriptPath: Path.Combine(Path.GetTempPath(), "missing-x360-protocol.py"),
            shadercLibraryPath: Path.Combine(Path.GetTempPath(), "missing-x360-shaderc.dll")));
        ShaderCompileRequest request = CreateRequest(defaultSpace: 1);

        Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(
            () => backend.Compile(request, new EmptyIncludeResolver()));

        Assert.Contains("default space", exception.Diagnostic, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Duplicate_define_names_are_rejected_instead_of_collapsing_to_a_map() {
        var backend = new Xbox360ShaderBackend(new Xbox360ShaderCompilerOptions(
            pythonExecutable: "missing-python-for-duplicate-defines",
            protocolScriptPath: Path.Combine(Path.GetTempPath(), "missing-x360-protocol.py"),
            shadercLibraryPath: Path.Combine(Path.GetTempPath(), "missing-x360-shaderc.dll")));
        ShaderCompileRequest request = CreateRequest(defines: [new ShaderDefine("USE_COLOR", "1"), new ShaderDefine("USE_COLOR", "0")]);

        Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(
            () => backend.Compile(request, new EmptyIncludeResolver()));

        Assert.Contains("duplicate define", exception.Diagnostic, StringComparison.OrdinalIgnoreCase);
    }

    static ShaderCompileRequest CreateRequest(
        ShaderCompileTarget target = ShaderCompileTarget.Xbox360,
        ShaderStage stage = ShaderStage.Vertex,
        ShaderModel model = null,
        int defaultSpace = 0,
        IReadOnlyList<ShaderDefine> defines = null) {
        return new ShaderCompileRequest(
            new ShaderSourceInfo("sample.hlsl", "float4 VSMain() : SV_Position { return 0; }"),
            "sample-program",
            "VSMain",
            stage,
            target,
            model ?? new ShaderModel(4, 0),
            "sample-variant",
            defines ?? Array.Empty<ShaderDefine>(),
            new ShaderCompileOptions(new ShaderBindingPolicy(defaultSpace, 0, 100, 200, 300), false, false, false));
    }

    sealed class EmptyIncludeResolver : IShaderIncludeResolver {
        public ShaderIncludeResult Resolve(string requestingFile, string includePath) =>
            throw new InvalidOperationException("This request does not include source files.");
    }
}
