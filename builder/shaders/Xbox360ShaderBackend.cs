using helengine;

namespace helengine.x360.builder;

/// <summary>Compiles the supported Xbox 360 shader subset through the host protocol worker.</summary>
public sealed class Xbox360ShaderBackend : IShaderBackend, IShaderBackendCachePolicy {
    static readonly ShaderBackendCapabilities BackendCapabilities = new(
        new ShaderModel(4, 0),
        new ShaderModel(4, 0),
        [ShaderStage.Vertex, ShaderStage.Pixel],
        false);

    readonly Xbox360ShaderCompiler Compiler;

    public Xbox360ShaderBackend() : this(new Xbox360ShaderCompilerOptions()) {
    }

    public Xbox360ShaderBackend(Xbox360ShaderCompilerOptions options) {
        Compiler = new Xbox360ShaderCompiler(options ?? throw new ArgumentNullException(nameof(options)));
    }

    public ShaderCompileTarget Target => ShaderCompileTarget.Xbox360;
    public ShaderBackendCapabilities Capabilities => BackendCapabilities;
    public bool UseSharedCache => false;

    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        }

        if (includeResolver == null) {
            throw new ArgumentNullException(nameof(includeResolver));
        }

        ValidateRequest(request);
        return Compiler.Compile(request, includeResolver);
    }

    internal static void ValidateRequest(ShaderCompileRequest request) {
        if (request.Target != ShaderCompileTarget.Xbox360) {
            throw Failure(request, "Unsupported target; this backend only supports Xbox360.");
        }

        if (request.Stage != ShaderStage.Vertex && request.Stage != ShaderStage.Pixel) {
            throw Failure(request, $"Unsupported stage '{request.Stage}'; only vertex and pixel shaders are supported.");
        }

        if (request.ShaderModel.Major != 4 || request.ShaderModel.Minor != 0) {
            throw Failure(request, $"Unsupported shader model '{request.ShaderModel}'; only shader model 4.0 is supported.");
        }

        if (request.Options.BindingPolicy.DefaultSpace != 0) {
            throw Failure(request, "The binding policy default space must be zero for Xbox 360 shader compilation.");
        }

        HashSet<string> defineNames = new(StringComparer.Ordinal);
        foreach (ShaderDefine define in request.Defines) {
            if (!defineNames.Add(define.Name)) {
                throw Failure(request, $"Duplicate define name '{define.Name}' cannot be represented by the protocol define map.");
            }
        }
    }

    static Xbox360ShaderCompilationException Failure(ShaderCompileRequest request, string diagnostic) =>
        new(request, diagnostic);
}
