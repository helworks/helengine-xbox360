using System.Security.Cryptography;
using System.Text;
using helengine;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.x360.builder;

/// <summary>Compiles explicit shared shader selections into X3SP pairs.</summary>
public sealed class Xbox360ShaderArtifactCooker {
    public const string BatchPath = "shaders/spritebatch-textured.x3sp";
    readonly Xbox360ShaderCompilerOptions Options;

    public Xbox360ShaderArtifactCooker(Xbox360ShaderCompilerOptions options = null) {
        Options = options ?? new Xbox360ShaderCompilerOptions();
    }

    public PlatformShaderArtifactCookResult Cook(PlatformShaderArtifactCookRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        if (request.PlatformId != "x360" || request.GraphicsProfileId != Xbox360MaterialSchemaIds.RendererFamilyId)
            throw new InvalidOperationException("X360_SHADER_PROFILE: expected x360/x360-unlit.");
        var sources = request.ShaderSources.ToDictionary(source => source.ShaderAssetId, StringComparer.Ordinal);
        var selections = new SortedDictionary<string, PlatformShaderDependency>(StringComparer.Ordinal);
        foreach (var dependency in request.ShaderDependencies) {
            _ = SelectVariant(dependency);
            if (!sources.TryGetValue(dependency.ShaderAssetId, out var source) || !source.HasSourcePath || string.IsNullOrWhiteSpace(source.SourceText))
                throw new InvalidOperationException("X360_SHADER_SOURCE: missing source snapshot/path.");
            selections.TryAdd(RelativePath(dependency), dependency);
        }

        // No output is touched until every requested pair compiles and serializes.
        var prepared = new List<(string Path, PlatformShaderDependency Dependency, byte[] Bytes)>();
        foreach (var selection in selections) {
            var dependency = selection.Value;
            var source = sources[dependency.ShaderAssetId];
            var variant = SelectVariant(dependency);
            var layout = dependency.ShaderAssetId == "SpriteBatchShader" ? Xbox360ShaderVertexLayout.Batch2D : Xbox360ShaderVertexLayout.Mesh3D;
            var backend = new Xbox360ShaderBackend(new Xbox360ShaderCompilerOptions(Options.PythonExecutable,
                Options.ProtocolScriptPath, Options.ShadercLibraryPath, Options.Timeout, layout));
            var resolver = new ShaderFilesystemIncludeResolver(Path.GetDirectoryName(source.SourcePath));
            Xbox360ShaderProgram Compile(ShaderStage stage, string name, string entry) {
                var defines = variant.Defines.Select(value => {
                    var parts = value.Split('=', 2);
                    return new ShaderDefine(parts[0], parts.Length == 2 ? parts[1] : "1");
                }).ToArray();
                var model = new ShaderModel(4, 0);
                var compileRequest = new ShaderCompileRequest(new ShaderSourceInfo(source.SourcePath, source.SourceText),
                    name, entry, stage, ShaderCompileTarget.Xbox360, model, variant.Name,
                    ShaderPlatformDefines.BuildDefines(ShaderCompileTarget.Xbox360, model, defines),
                    new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, false));
                return Xbox360ShaderProgramFactory.FromCompilation((Xbox360ShaderCompileResult)backend.Compile(compileRequest, resolver));
            }
            var pair = Xbox360ShaderProgramPair.Link(Compile(ShaderStage.Vertex, dependency.VertexProgramName, variant.VertexEntryPoint),
                Compile(ShaderStage.Pixel, dependency.PixelProgramName, variant.PixelEntryPoint));
            if (dependency.VariantName == Xbox360MaterialSchemaIds.MeshVariant) Xbox360MeshMaterialContract.Validate(pair);
            prepared.Add((selection.Key, dependency, Xbox360ShaderArtifactSerializer.Serialize(new Xbox360ShaderArtifact(dependency.ShaderAssetId, pair))));
        }

        foreach (var item in prepared) {
            string destination = Path.Combine(request.CookRootPath, item.Path);
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            string temporary = destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try {
                File.WriteAllBytes(temporary, item.Bytes);
                File.Move(temporary, destination, true);
            } finally {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }
        return new PlatformShaderArtifactCookResult(prepared.Select(item =>
            new PlatformCookedArtifactDeclaration(item.Path, item.Path, "shader", "x360")).ToArray());
    }

    public static string RelativePath(PlatformShaderDependency dependency) {
        ArgumentNullException.ThrowIfNull(dependency);
        _ = SelectVariant(dependency);
        if (dependency.ShaderAssetId == "SpriteBatchShader") return BatchPath;
        using var bytes = new MemoryStream();
        using (var writer = new BinaryWriter(bytes, new UTF8Encoding(false, true), true)) {
            foreach (string value in new[] { dependency.ShaderAssetId, dependency.VertexProgramName, dependency.PixelProgramName, dependency.VariantName })
                writer.Write(value);
        }
        return "shaders/" + Convert.ToHexString(SHA256.HashData(bytes.ToArray())).ToLowerInvariant() + ".x3sp";
    }

    static StandardShaderVariant SelectVariant(PlatformShaderDependency dependency) {
        if (!dependency.HasProgramPair) throw new InvalidOperationException("X360_SHADER_SELECTION: explicit pair and variant required.");
        if (dependency.ShaderAssetId == "SpriteBatchShader") {
            if (dependency.VertexProgramName != "SpriteBatchShader.vs" || dependency.PixelProgramName != "SpriteBatchShader.ps" || dependency.VariantName != "Textured")
                throw new InvalidOperationException("X360_BATCH_SELECTION: only the shared Textured pair is supported.");
            return SpriteBatchShaderVariants.All.Single(variant => variant.Name == "Textured");
        }
        if ((dependency.VariantName != "default" && dependency.VariantName != Xbox360MaterialSchemaIds.MeshVariant) || dependency.ShaderAssetId == "StandardShader")
            throw new InvalidOperationException("X360_SHADER_SELECTION: authored Mesh3D requires default variant and VS/PS entrypoints; Standard is unsupported.");
        return new StandardShaderVariant(dependency.VariantName, "VS", "PS", []);
    }
}
