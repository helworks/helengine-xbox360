using System.Reflection;
using System.Text.Json.Nodes;
using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

internal static class Xbox360ShaderProgramTestSupport {
    const string ShadercLibraryPath = @"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll";

    static readonly Lazy<Xbox360ShaderCompileResult> TransformVertexValue = new(() => CompileFixture(
        "transform_vs.hlsl", ShaderStage.Vertex, "arbitrary-vertex-program"));
    static readonly Lazy<Xbox360ShaderCompileResult> TexturedPixelValue = new(() => CompileFixture(
        "texture_ps.hlsl", ShaderStage.Pixel, "arbitrary-pixel-program"));
    static readonly Lazy<Xbox360ShaderCompileResult> ColorOnlyPixelValue = new(() => CompileFixture(
        "flat_ps.hlsl", ShaderStage.Pixel, "generic-color-fragment"));
    static readonly Lazy<Xbox360ShaderCompileResult> UniformPixelValue = new(() => CompileFixture(
        "uniform_color_ps.hlsl", ShaderStage.Pixel, "unrelated-uniform-program"));

    static readonly ConstructorInfo CompileResultConstructor = typeof(Xbox360ShaderCompileResult)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic)
        .Single(constructor => constructor.GetParameters().Length == 9);

    public static Xbox360ShaderCompileResult TransformVertex => TransformVertexValue.Value;
    public static Xbox360ShaderCompileResult TexturedPixel => TexturedPixelValue.Value;
    public static Xbox360ShaderCompileResult ColorOnlyPixel => ColorOnlyPixelValue.Value;
    public static Xbox360ShaderCompileResult UniformPixel => UniformPixelValue.Value;

    public static Xbox360ShaderCompileResult CloneResult(
        Xbox360ShaderCompileResult source,
        ShaderCompileRequest request = null,
        ShaderProgramDefinition programDefinition = null,
        ShaderCompiledBinary binary = null,
        string nativeMetadataJson = null,
        string toolIdentity = null,
        string compileIdentity = null,
        Xbox360ShaderVertexLayout? vertexLayout = null) =>
        (Xbox360ShaderCompileResult)CompileResultConstructor.Invoke([
            request ?? CloneRequest(source.Request),
            programDefinition ?? CloneDefinition(source.ProgramDefinition),
            binary ?? CloneBinary(source.Binary),
            source.Diagnostics,
            nativeMetadataJson ?? source.NativeMetadataJson,
            toolIdentity ?? source.ToolIdentity,
            compileIdentity ?? source.CompileIdentity,
            source.IncludedSources,
            vertexLayout ?? source.VertexLayout
        ]);

    public static ShaderCompileRequest CloneRequest(
        ShaderCompileRequest source,
        string sourcePath = null,
        string programName = null,
        string entryPoint = null,
        ShaderStage? stage = null,
        ShaderCompileTarget? target = null,
        ShaderModel shaderModel = null,
        string variant = null,
        ShaderBindingPolicy bindingPolicy = null) => new(
            new ShaderSourceInfo(sourcePath ?? source.Source.Path, source.Source.Source),
            programName ?? source.ProgramName,
            entryPoint ?? source.EntryPoint,
            stage ?? source.Stage,
            target ?? source.Target,
            shaderModel ?? source.ShaderModel,
            variant ?? source.Variant,
            source.Defines.ToArray(),
            new ShaderCompileOptions(
                bindingPolicy ?? source.Options.BindingPolicy,
                source.Options.GenerateDebugInfo,
                source.Options.Optimize,
                source.Options.TreatWarningsAsErrors));

    public static ShaderProgramDefinition CloneDefinition(
        ShaderProgramDefinition source,
        string name = null,
        ShaderStage? stage = null,
        string entryPoint = null,
        ShaderBinding[] bindings = null,
        ShaderVertexElement[] inputs = null,
        ShaderVertexElement[] outputs = null,
        ShaderVariant[] variants = null) => new(
            name ?? source.Name,
            stage ?? source.Stage,
            entryPoint ?? source.EntryPoint,
            bindings ?? source.Bindings.Select(binding => new ShaderBinding(
                binding.Name,
                binding.Type,
                binding.Set,
                binding.Slot,
                binding.Size,
                binding.Members.Select(member => new ShaderConstantMember(
                    member.Name, member.Type, member.Offset, member.Size)).ToArray())).ToArray(),
            inputs ?? source.Inputs.Select(element => new ShaderVertexElement(
                element.Semantic, element.Index, element.Format)).ToArray(),
            outputs ?? source.Outputs.Select(element => new ShaderVertexElement(
                element.Semantic, element.Index, element.Format)).ToArray(),
            variants ?? source.Variants.Select(variant => new ShaderVariant(
                variant.Name, variant.Defines.ToArray())).ToArray());

    public static ShaderCompiledBinary CloneBinary(
        ShaderCompiledBinary source,
        ShaderCompileTarget? target = null,
        ShaderStage? stage = null,
        string entryPoint = null,
        string variant = null,
        byte[] bytecode = null) => new(
            target ?? source.Target,
            stage ?? source.Stage,
            entryPoint ?? source.EntryPoint,
            variant ?? source.Variant,
            bytecode ?? source.Bytecode.ToArray());

    public static string EditMetadata(Xbox360ShaderCompileResult source, Action<JsonObject> edit) {
        JsonObject metadata = JsonNode.Parse(source.NativeMetadataJson).AsObject();
        edit(metadata);
        return metadata.ToJsonString();
    }

    public static Xbox360ShaderCompileResult Reidentify(
        Xbox360ShaderCompileResult source,
        string programName = null,
        string entryPoint = null,
        string variant = null,
        string toolIdentity = null) {
        string selectedEntryPoint = entryPoint ?? source.Request.EntryPoint;
        string selectedVariant = variant ?? source.Request.Variant;
        ShaderCompileRequest request = CloneRequest(source.Request,
            programName: programName, entryPoint: selectedEntryPoint, variant: selectedVariant);
        ShaderVariant[] variants = source.ProgramDefinition.Variants
            .Select(item => new ShaderVariant(selectedVariant, item.Defines.ToArray())).ToArray();
        ShaderProgramDefinition definition = CloneDefinition(source.ProgramDefinition,
            name: programName, entryPoint: selectedEntryPoint, variants: variants);
        ShaderCompiledBinary binary = CloneBinary(source.Binary,
            entryPoint: selectedEntryPoint, variant: selectedVariant);
        string metadata = EditMetadata(source, root => root["entrypoint"] = selectedEntryPoint);
        return CloneResult(source, request, definition, binary, metadata, toolIdentity);
    }

    static Xbox360ShaderCompileResult CompileFixture(
        string fixtureName,
        ShaderStage stage,
        string programName) {
        if (!File.Exists(ShadercLibraryPath)) {
            throw new FileNotFoundException("The real shaderc integration prerequisite is missing.", ShadercLibraryPath);
        }

        string root = FindRepositoryRoot();
        string fixturePath = Path.Combine(root, "tests", "shaders", "fixtures", fixtureName);
        string source = File.ReadAllText(fixturePath);
        if (string.Equals(fixtureName, "flat_ps.hlsl", StringComparison.Ordinal)) {
            source = source.Replace("nointerpolation ", string.Empty, StringComparison.Ordinal);
        }
        string sourcePath = $"source/{fixtureName}";
        ShaderCompileRequest request = new(
            new ShaderSourceInfo(sourcePath, source),
            programName,
            "main",
            stage,
            ShaderCompileTarget.Xbox360,
            new ShaderModel(4, 0),
            "release-candidate",
            Array.Empty<ShaderDefine>(),
            new ShaderCompileOptions(new ShaderBindingPolicy(0, 17, 101, 202, 300), false, false, false));

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
            if (!File.Exists(path)) {
                throw new FileNotFoundException("Expected the builder-bundled shader tool.", path);
            }
        }

        Xbox360ShaderBackend backend = new(new Xbox360ShaderCompilerOptions(
            shadercLibraryPath: ShadercLibraryPath,
            timeout: TimeSpan.FromSeconds(30)));
        ShaderCompileResult result = backend.Compile(request, new EmptyIncludeResolver());
        return result as Xbox360ShaderCompileResult
            ?? throw new InvalidOperationException("The real Xbox 360 shader backend returned an unexpected result type.");
    }

    static string FindRepositoryRoot() {
        for (DirectoryInfo directory = new(AppContext.BaseDirectory); directory != null; directory = directory.Parent) {
            if (File.Exists(Path.Combine(directory.FullName, "tests", "shaders", "fixtures", "transform_vs.hlsl"))) {
                return directory.FullName;
            }
        }
        throw new DirectoryNotFoundException("Could not locate the checkout containing tests/shaders/fixtures.");
    }

    sealed class EmptyIncludeResolver : IShaderIncludeResolver {
        public ShaderIncludeResult Resolve(string requestingFile, string includePath) =>
            throw new InvalidOperationException($"Unexpected include '{includePath}' from '{requestingFile}'.");
    }
}
