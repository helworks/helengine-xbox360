namespace helengine.x360.builder;

/// <summary>Configures the host Python protocol process used by the Xbox 360 shader backend.</summary>
public sealed class Xbox360ShaderCompilerOptions {
    public Xbox360ShaderCompilerOptions(
        string pythonExecutable = null,
        string protocolScriptPath = null,
        string shadercLibraryPath = null,
        TimeSpan? timeout = null,
        Xbox360ShaderVertexLayout vertexLayout = Xbox360ShaderVertexLayout.Mesh3D) {
        _ = Xbox360ShaderVertexLayouts.ToProtocolValue(vertexLayout);
        TimeSpan effectiveTimeout = timeout ?? TimeSpan.FromSeconds(60);
        if (effectiveTimeout <= TimeSpan.Zero) {
            throw new ArgumentOutOfRangeException(nameof(timeout), "The shader compiler timeout must be positive.");
        }

        PythonExecutable = pythonExecutable;
        ProtocolScriptPath = protocolScriptPath;
        ShadercLibraryPath = shadercLibraryPath;
        Timeout = effectiveTimeout;
        VertexLayout = vertexLayout;
    }

    public string PythonExecutable { get; }
    public string ProtocolScriptPath { get; }
    public string ShadercLibraryPath { get; }
    public TimeSpan Timeout { get; }
    public Xbox360ShaderVertexLayout VertexLayout { get; }
}
