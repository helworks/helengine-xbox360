using System.Diagnostics;
using System.Text.Json;
using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderCompilerProcessTests {
    [Fact]
    public void Batch2d_profile_is_sent_and_retained_only_when_reply_metadata_matches() {
        using var fixture = new ProtocolFixture("success");
        Xbox360ShaderBackend backend = fixture.CreateBackend(vertexLayout: Xbox360ShaderVertexLayout.Batch2D);

        Xbox360ShaderCompileResult result = Assert.IsType<Xbox360ShaderCompileResult>(
            backend.Compile(CreateRequest(), new EmptyIncludeResolver()));

        Assert.Equal(Xbox360ShaderVertexLayout.Batch2D, result.VertexLayout);
        using JsonDocument metadata = JsonDocument.Parse(result.NativeMetadataJson);
        Assert.Equal("batch2d-v1", metadata.RootElement.GetProperty("vertex_layout").GetString());
        Assert.Equal("batch2d-v1", metadata.RootElement.GetProperty("wire_request").GetProperty("vertex_layout").GetString());
    }

    [Theory]
    [InlineData("missing-vertex-layout", "vertex_layout")]
    [InlineData("unknown-vertex-layout", "vertex_layout")]
    [InlineData("wrong-vertex-layout", "vertex_layout")]
    public void Reply_with_missing_unknown_or_wrong_vertex_layout_is_rejected(string mode, string message) {
        using var fixture = new ProtocolFixture(mode);

        Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(() =>
            fixture.CreateBackend(vertexLayout: Xbox360ShaderVertexLayout.Batch2D)
                .Compile(CreateRequest(), new EmptyIncludeResolver()));

        Assert.Contains(message, exception.Diagnostic, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Invalid_vertex_layout_option_is_rejected_before_starting_the_protocol_child() {
        Assert.Throws<ArgumentOutOfRangeException>(() => new Xbox360ShaderCompilerOptions(
            vertexLayout: (Xbox360ShaderVertexLayout)77));
    }

    [Fact]
    public void Success_maps_physical_reflection_to_generic_logical_shifts_and_keeps_host_metadata() {
        using var fixture = new ProtocolFixture("success");
        ShaderCompileRequest request = CreateRequest(
            ShaderStage.Pixel,
            "PSShade",
            "textured-program",
            "textured-variant",
            [new ShaderDefine("USE_TEXTURE", "1")],
            new ShaderBindingPolicy(0, 17, 101, 203, 307),
            new ShaderCompileOptions(new ShaderBindingPolicy(0, 17, 101, 203, 307), true, true, false));

        Xbox360ShaderCompileResult result = Assert.IsType<Xbox360ShaderCompileResult>(
            fixture.CreateBackend().Compile(request, new EmptyIncludeResolver()));

        Assert.True(result.Success);
        Assert.Equal(Xbox360ShaderVertexLayout.Mesh3D, result.VertexLayout);
        Assert.Equal("textured-program", result.ProgramDefinition.Name);
        Assert.Equal("PSShade", result.ProgramDefinition.EntryPoint);
        Assert.Equal("textured-variant", result.Binary.Variant);
        Assert.Equal(new byte[] { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef }, result.Binary.Bytecode);
        Assert.Equal("LOCATION", result.ProgramDefinition.Inputs[0].Semantic);
        Assert.Equal(2, result.ProgramDefinition.Inputs[0].Index);
        Assert.Equal("float2", result.ProgramDefinition.Inputs[0].Format);
        Assert.Equal("LOCATION", result.ProgramDefinition.Outputs[0].Semantic);
        Assert.Equal(0, result.ProgramDefinition.Outputs[0].Index);
        Assert.Equal("float4", result.ProgramDefinition.Outputs[0].Format);
        Assert.Equal(3, result.ProgramDefinition.Bindings.Length);
        Assert.Equal(17, result.ProgramDefinition.Bindings[0].Slot);
        Assert.Equal(102, result.ProgramDefinition.Bindings[1].Slot);
        Assert.Equal(205, result.ProgramDefinition.Bindings[2].Slot);
        Assert.Equal("Tint", result.ProgramDefinition.Bindings[0].Members[0].Name);
        Assert.Equal("float4", result.ProgramDefinition.Bindings[0].Members[0].Type);
        Assert.Equal(0, result.ProgramDefinition.Bindings[0].Members[0].Offset);
        Assert.Equal(16, result.ProgramDefinition.Bindings[0].Members[0].Size);
        Assert.Equal("USE_TEXTURE=1", result.ProgramDefinition.Variants[0].Defines[0]);
        Assert.Equal(new string('A', 64), result.ToolIdentity);
        Assert.Equal(new string('B', 64), result.CompileIdentity);
        Assert.Equal(ShaderDiagnosticSeverity.Warning, result.Diagnostics[0].Severity);
        Assert.Contains("warning: include path kept intact: common.hlsli", result.Diagnostics[0].Message);
        Assert.Equal("sample.hlsl", result.Diagnostics[0].FilePath);
        Assert.Equal(0, result.Diagnostics[0].Line);
        Assert.Equal(0, result.Diagnostics[0].Column);

        using JsonDocument metadata = JsonDocument.Parse(result.NativeMetadataJson);
        JsonElement wireRequest = metadata.RootElement.GetProperty("wire_request");
        Assert.Equal("x360", wireRequest.GetProperty("target").GetString());
        Assert.Equal("4.0", wireRequest.GetProperty("shader_model").GetString());
        Assert.True(wireRequest.GetProperty("options").GetProperty("debug_info").GetBoolean());
        Assert.True(wireRequest.GetProperty("options").GetProperty("optimize").GetBoolean());
        Assert.Equal(0, wireRequest.GetProperty("binding_policy").GetProperty("default_space").GetInt32());
        Assert.Equal(17, wireRequest.GetProperty("binding_policy").GetProperty("constant_buffer_shift").GetInt32());
        Assert.Equal(101, wireRequest.GetProperty("binding_policy").GetProperty("texture_shift").GetInt32());
        Assert.Equal(203, wireRequest.GetProperty("binding_policy").GetProperty("sampler_shift").GetInt32());
        Assert.Equal(307, wireRequest.GetProperty("binding_policy").GetProperty("storage_shift").GetInt32());
        Assert.Equal("mesh3d-v1", wireRequest.GetProperty("vertex_layout").GetString());
    }

    [Fact]
    public void Include_requests_are_resolved_once_and_provenance_is_preserved() {
        using var fixture = new ProtocolFixture("include");
        var resolver = new RecordingIncludeResolver();

        Xbox360ShaderCompileResult result = Assert.IsType<Xbox360ShaderCompileResult>(
            fixture.CreateBackend().Compile(CreateRequest(), resolver));

        Assert.Equal(1, resolver.ResolveCount);
        Assert.Equal("sample.hlsl", resolver.RequestingFile);
        Assert.Equal("common.hlsli", resolver.IncludePath);
        Assert.Single(result.IncludedSources);
        Assert.Equal("resolved/common.hlsli", result.IncludedSources[0].SourceName);
        Assert.Equal(new string('C', 64), result.IncludedSources[0].Sha256);
    }

    [Theory]
    [InlineData("malformed", "JSON")]
    [InlineData("eof", "EOF")]
    [InlineData("nonzero", "fixture shader failure")]
    [InlineData("exit-mismatch", "exit code")]
    [InlineData("bad-base64", "base64")]
    [InlineData("bad-alignment", "dword")]
    [InlineData("wrong-stage", "stage")]
    [InlineData("wrong-entrypoint", "entrypoint")]
    [InlineData("oversized-line", "8MiB")]
    public void Invalid_protocol_results_fail_with_diagnostics(string mode, string expectedDiagnostic) {
        using var fixture = new ProtocolFixture(mode);

        Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(() => {
            fixture.CreateBackend().Compile(CreateRequest(), new EmptyIncludeResolver());
        });

        Assert.Contains(expectedDiagnostic, exception.Diagnostic, StringComparison.OrdinalIgnoreCase);
        Assert.Equal("sample.hlsl", exception.SourcePath);
        Assert.Equal("sample-program", exception.ProgramName);
        Assert.Equal("VSMain", exception.EntryPoint);
    }

    [Fact]
    public void Timeout_terminates_the_child_started_for_this_compile() {
        string pidPath = Path.Combine(Path.GetTempPath(), $"x360-protocol-child-{Guid.NewGuid():N}.pid");
        using var fixture = new ProtocolFixture($"timeout|{pidPath}");
        try {
            Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(() => {
                fixture.CreateBackend(TimeSpan.FromMilliseconds(350)).Compile(CreateRequest(), new EmptyIncludeResolver());
            });

            Assert.Contains("timeout", exception.Diagnostic, StringComparison.OrdinalIgnoreCase);
            int childId = int.Parse(File.ReadAllText(pidPath));
            bool childExited;
            try {
                using Process child = Process.GetProcessById(childId);
                childExited = child.WaitForExit(5000);
            } catch (ArgumentException) {
                childExited = true;
            }
            Assert.True(childExited, "The timed-out protocol child must exit.");
        } finally {
            if (File.Exists(pidPath)) {
                File.Delete(pidPath);
            }
        }
    }

    [Fact]
    public void Missing_tools_fail_at_compile_time_with_request_context() {
        string missing = Path.Combine(Path.GetTempPath(), $"x360-missing-{Guid.NewGuid():N}");
        var backend = new Xbox360ShaderBackend(new Xbox360ShaderCompilerOptions(
            pythonExecutable: missing,
            protocolScriptPath: missing + ".py",
            shadercLibraryPath: missing + ".dll"));

        Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(() => {
            backend.Compile(CreateRequest(), new EmptyIncludeResolver());
        });

        Assert.Contains("protocol script", exception.Diagnostic, StringComparison.OrdinalIgnoreCase);
        Assert.Equal("sample-program", exception.ProgramName);
    }

    [Fact]
    public void Unrepresentable_timeout_fails_before_starting_the_protocol_child() {
        string launchMarkerPath = Path.Combine(Path.GetTempPath(), $"x360-protocol-launch-{Guid.NewGuid():N}.marker");
        using var fixture = new ProtocolFixture($"launch-marker|{launchMarkerPath}");
        try {
            Xbox360ShaderCompilationException exception = Assert.Throws<Xbox360ShaderCompilationException>(() => {
                fixture.CreateBackend(TimeSpan.MaxValue).Compile(CreateRequest(), new EmptyIncludeResolver());
            });

            Assert.False(string.IsNullOrWhiteSpace(exception.Diagnostic));
            bool childStarted = SpinWait.SpinUntil(() => File.Exists(launchMarkerPath), TimeSpan.FromSeconds(2));
            Assert.False(childStarted, "An invalid timeout must be rejected before creating the protocol child.");
        } finally {
            if (File.Exists(launchMarkerPath)) {
                File.Delete(launchMarkerPath);
            }
        }
    }

    static ShaderCompileRequest CreateRequest(
        ShaderStage stage = ShaderStage.Vertex,
        string entryPoint = "VSMain",
        string programName = "sample-program",
        string variant = "sample-variant",
        IReadOnlyList<ShaderDefine> defines = null,
        ShaderBindingPolicy bindingPolicy = null,
        ShaderCompileOptions options = null) {
        bindingPolicy ??= new ShaderBindingPolicy(0, 0, 100, 200, 300);
        options ??= new ShaderCompileOptions(bindingPolicy, false, false, false);
        return new ShaderCompileRequest(
            new ShaderSourceInfo("sample.hlsl", "float4 VSMain() : SV_Position { return 0; }"),
            programName,
            entryPoint,
            stage,
            ShaderCompileTarget.Xbox360,
            new ShaderModel(4, 0),
            variant,
            defines ?? Array.Empty<ShaderDefine>(),
            options);
    }

    sealed class EmptyIncludeResolver : IShaderIncludeResolver {
        public ShaderIncludeResult Resolve(string requestingFile, string includePath) =>
            throw new InvalidOperationException("No include was expected in this fixture.");
    }

    sealed class RecordingIncludeResolver : IShaderIncludeResolver {
        public int ResolveCount { get; private set; }
        public string RequestingFile { get; private set; }
        public string IncludePath { get; private set; }

        public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
            ResolveCount++;
            RequestingFile = requestingFile;
            IncludePath = includePath;
            return new ShaderIncludeResult("resolved/common.hlsli", "float4 SharedTint;");
        }
    }

    sealed class ProtocolFixture : IDisposable {
        readonly string DirectoryPath;
        readonly string ShadercMarkerPath;
        readonly string ScriptPath;

        public ProtocolFixture(string mode) {
            DirectoryPath = Path.Combine(Path.GetTempPath(), $"x360-protocol-fixture-{Guid.NewGuid():N}");
            Directory.CreateDirectory(DirectoryPath);
            ShadercMarkerPath = Path.Combine(DirectoryPath, "shaderc.fixture");
            ScriptPath = Path.Combine(DirectoryPath, "backend_protocol.py");
            File.WriteAllText(ShadercMarkerPath, mode);
            File.WriteAllText(ScriptPath, "fixture process marker; this is not the protocol worker");
        }

        public Xbox360ShaderBackend CreateBackend(
            TimeSpan? timeout = null,
            Xbox360ShaderVertexLayout vertexLayout = Xbox360ShaderVertexLayout.Mesh3D) {
            string fixtureExe = Path.Combine(AppContext.BaseDirectory, "shader-protocol-fixture", "Xbox360ShaderProtocolFixture.exe");
            Assert.True(File.Exists(fixtureExe), $"Expected the C# process fixture at '{fixtureExe}'.");
            return new Xbox360ShaderBackend(new Xbox360ShaderCompilerOptions(
                pythonExecutable: fixtureExe,
                protocolScriptPath: ScriptPath,
                shadercLibraryPath: ShadercMarkerPath,
                timeout: timeout ?? TimeSpan.FromSeconds(5),
                vertexLayout: vertexLayout));
        }

        public void Dispose() {
            if (Directory.Exists(DirectoryPath)) {
                Directory.Delete(DirectoryPath, recursive: true);
            }
        }
    }
}
