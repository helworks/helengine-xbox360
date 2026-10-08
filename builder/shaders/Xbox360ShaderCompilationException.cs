using helengine;

namespace helengine.x360.builder;

/// <summary>Reports a failed Xenos compile together with the request that produced it.</summary>
public sealed class Xbox360ShaderCompilationException : InvalidOperationException {
    public Xbox360ShaderCompilationException(ShaderCompileRequest request, string diagnostic, Exception innerException = null)
        : base(BuildMessage(request, diagnostic), innerException) {
        Request = request ?? throw new ArgumentNullException(nameof(request));
        Diagnostic = string.IsNullOrWhiteSpace(diagnostic)
            ? "The Xbox 360 shader compiler failed without a diagnostic."
            : diagnostic;
    }

    public ShaderCompileRequest Request { get; }
    public string Diagnostic { get; }
    public string SourcePath => Request.Source.Path;
    public string ProgramName => Request.ProgramName;
    public string EntryPoint => Request.EntryPoint;
    public ShaderStage Stage => Request.Stage;
    public string Variant => Request.Variant;

    static string BuildMessage(ShaderCompileRequest request, string diagnostic) {
        if (request == null) {
            return diagnostic ?? "The Xbox 360 shader compiler failed.";
        }

        return $"Xbox 360 shader compilation failed for source '{request.Source.Path}', program '{request.ProgramName}', entry '{request.EntryPoint}', stage '{request.Stage}', variant '{request.Variant}': {diagnostic}";
    }
}
