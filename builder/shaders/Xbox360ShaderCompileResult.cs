using helengine;

namespace helengine.x360.builder;

/// <summary>Host-only shader result carrying compiler metadata for a future cooker.</summary>
public sealed class Xbox360ShaderCompileResult : ShaderCompileResult {
    internal Xbox360ShaderCompileResult(
        ShaderCompileRequest request,
        ShaderProgramDefinition programDefinition,
        ShaderCompiledBinary binary,
        IReadOnlyList<ShaderCompileDiagnostic> diagnostics,
        string nativeMetadataJson,
        string toolIdentity,
        string compileIdentity,
        IReadOnlyList<Xbox360IncludedSource> includedSources,
        Xbox360ShaderVertexLayout vertexLayout)
        : base(request, programDefinition, binary, diagnostics, success: true) {
        NativeMetadataJson = nativeMetadataJson;
        ToolIdentity = toolIdentity;
        CompileIdentity = compileIdentity;
        IncludedSources = includedSources;
        VertexLayout = vertexLayout;
    }

    public string NativeMetadataJson { get; }
    public string ToolIdentity { get; }
    public string CompileIdentity { get; }
    public IReadOnlyList<Xbox360IncludedSource> IncludedSources { get; }
    public Xbox360ShaderVertexLayout VertexLayout { get; }
}

/// <summary>Identity of an include actually consumed by shaderc.</summary>
public sealed record Xbox360IncludedSource(string SourceName, string Sha256);
