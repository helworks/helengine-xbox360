using helengine;
using helengine.baseplatform.Requests;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360MeshMaterialTests {
    [Fact]
    public void Material_dependency_cooks_real_pair_and_shared_payload_roundtrips() {
        string root = AppContext.BaseDirectory;
        while (!File.Exists(Path.Combine(root, "builder", "Xbox360PlatformAssetBuilder.cs")))
            root = Directory.GetParent(root)?.FullName ?? throw new DirectoryNotFoundException();
        var fields = new Dictionary<string, string> {
            ["shader-asset-id"] = "mesh-material", ["vertex-program"] = "mesh.vs", ["pixel-program"] = "mesh.ps", ["base-color"] = "#804020FF"
        };
        var request = new PlatformMaterialCookRequest("material", "materials/test", "x360", "release", "x360-unlit", Xbox360MaterialSchemaIds.MeshShaderSchemaId, fields, numericPolicy: global::helengine.NumericPrecisionPolicy.Default);
        var cooked = new Xbox360MaterialCooker().Cook(request);
        var dependency = Assert.Single(cooked.ReferencedShaderDependencies);
        using var stream = new MemoryStream(cooked.CookedMaterialBytes);
        using var material = ShaderMaterialAssetBinarySerializer.Deserialize(stream);
        Assert.Equal(dependency.ShaderAssetId, material.ShaderAssetId);
        Assert.Equal("MeshMaterialV1", material.Variant);
        Assert.False(material.CastsShadows);
        Assert.False(material.ReceivesShadows);
        var color = Assert.Single(material.ConstantBuffers);
        Assert.Equal("Material", color.Name);
        Assert.Equal(128 / 255f, System.Buffers.Binary.BinaryPrimitives.ReadSingleLittleEndian(color.Data));
        Assert.Equal(MaterialBlendMode.Opaque, material.RenderState.BlendMode);
        Assert.Equal(MaterialCullMode.Back, material.RenderState.CullMode);
        string sourcePath = Path.Combine(root, "tests/shaders/mesh-material.hlsl");
        string text = File.ReadAllText(sourcePath);
        string output = Path.Combine(root, ".artifacts/mesh-material-tests", Guid.NewGuid().ToString("N"));
        var cooker = new Xbox360ShaderArtifactCooker(new(shadercLibraryPath: Path.GetFullPath(Path.Combine(root, "../helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll"))));
        PlatformShaderArtifactCookRequest ShaderRequest(string source) => new(output, "x360", "release", "x360-unlit", [dependency],
            [new("mesh-material", "snapshot", source, sourcePath)]);
        var declaration = Assert.Single(cooker.Cook(ShaderRequest(text)).CookedArtifactDeclarations);
        string path = Path.Combine(output, declaration.RelativePath);
        byte[] original = File.ReadAllBytes(path);
        Assert.Throws<InvalidOperationException>(() => cooker.Cook(ShaderRequest(text.Replace("baseColor", "wrongSemantic"))));
        Assert.Equal(original, File.ReadAllBytes(path));
        fields["shader-asset-id"] = "StandardShader";
        // The constructor snapshots fields, so the unsupported-selection case
        // needs a new request containing the changed shader identity.
        var unsupported = new PlatformMaterialCookRequest("material", "materials/test", "x360", "release",
            "x360-unlit", Xbox360MaterialSchemaIds.MeshShaderSchemaId, fields, numericPolicy: global::helengine.NumericPrecisionPolicy.Default);
        Assert.Throws<InvalidOperationException>(() => new Xbox360MaterialCooker().Cook(unsupported));
    }
}
