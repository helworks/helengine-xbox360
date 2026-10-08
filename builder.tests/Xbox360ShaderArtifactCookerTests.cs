using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderArtifactCookerTests {
    static PlatformShaderDependency Batch(string variant = "Textured") => new("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variant);

    [Fact]
    public void Builder_declares_the_shared_textured_pair() {
        var builder = new Xbox360PlatformAssetBuilder();
        var selection = Assert.Single(builder.RendererShaderDependencies);
        Assert.Equal("SpriteBatchShader", selection.ShaderAssetId);
        Assert.Equal("Textured", selection.VariantName);
        Assert.Equal(Xbox360ShaderArtifactCooker.BatchPath, Xbox360ShaderArtifactCooker.RelativePath(selection));
    }

    [Fact]
    public void Authored_paths_preserve_case_and_pair_identity_without_path_traversal() {
        var a = new PlatformShaderDependency("../asset", "v", "p", "default");
        var b = new PlatformShaderDependency("../Asset", "v", "p", "default");
        var c = new PlatformShaderDependency("../asset", "other", "p", "default");
        var path = Xbox360ShaderArtifactCooker.RelativePath(a);
        Assert.Matches("^shaders/[0-9a-f]{64}\\.x3sp$", path);
        Assert.NotEqual(path, Xbox360ShaderArtifactCooker.RelativePath(b));
        Assert.NotEqual(path, Xbox360ShaderArtifactCooker.RelativePath(c));
    }

    [Fact]
    public void Unsupported_selection_fails_before_publication() {
        Assert.Throws<InvalidOperationException>(() => Xbox360ShaderArtifactCooker.RelativePath(Batch("RoundedShape")));
        Assert.Throws<InvalidOperationException>(() => Xbox360ShaderArtifactCooker.RelativePath(new PlatformShaderDependency("StandardShader", "v", "p", "default")));
    }

    [Fact]
    public void Real_shared_source_cooks_deterministically_and_failure_preserves_existing_files() {
        string root = AppContext.BaseDirectory;
        while (!File.Exists(Path.Combine(root, "builder", "Xbox360PlatformAssetBuilder.cs")))
            root = Directory.GetParent(root)?.FullName ?? throw new DirectoryNotFoundException("Xbox360 checkout");
        string sourcePath = Path.GetFullPath(Path.Combine(root, "../helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl"));
        string shaderc = Path.GetFullPath(Path.Combine(root, "../helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll"));
        string output = Path.Combine(root, ".artifacts", "cooker-tests", Guid.NewGuid().ToString("N"));
        var cooker = new Xbox360ShaderArtifactCooker(new(shadercLibraryPath: shaderc));
        PlatformShaderArtifactCookRequest Request(string text) => new(output, "x360", "release", "x360-unlit", [Batch(), Batch()],
            [new PlatformShaderArtifactCookSource("SpriteBatchShader", "test-snapshot", text, sourcePath)]);
        var request = Request(File.ReadAllText(sourcePath));
        var declaration = Assert.Single(cooker.Cook(request).CookedArtifactDeclarations);
        string destination = Path.Combine(output, declaration.RelativePath);
        byte[] first = File.ReadAllBytes(destination);
        Assert.Equal("X3SP", System.Text.Encoding.ASCII.GetString(first, 0, 4));
        cooker.Cook(request);
        Assert.Equal(first, File.ReadAllBytes(destination));
        Assert.ThrowsAny<Exception>(() => cooker.Cook(Request("invalid hlsl")));
        Assert.Equal(first, File.ReadAllBytes(destination));
        Assert.Empty(Directory.GetFiles(Path.GetDirectoryName(destination)!, "*.tmp"));
    }
}
