using helengine.editor;

public static class CubeTestShaderPreparation {
    public static void Prepare(string stagedRoot, string shaderPath) {
        string assets = Path.Combine(Path.GetFullPath(stagedRoot), "assets");
        string material = Path.Combine(assets, "materials/rendering/cube_test/CubeTestSolid.hasset");
        string scene = Path.Combine(assets, "scenes/rendering/cube_test.helen");
        string output = material + ".x360.hasset";
        string shaderOutput = Path.Combine(assets, "shaders/X360CubeMesh.hlsl");
        if (!File.Exists(material) || !File.Exists(scene))
            throw new FileNotFoundException("The staged project must contain cube_test and CubeTestSolid.");
        if (File.Exists(output) || File.Exists(shaderOutput))
            throw new IOException("Use a fresh staged project; shader preparation never replaces existing assets.");
        byte[] shader = File.ReadAllBytes(shaderPath);
        var document = new MaterialAssetPlatformOverrideDocument { PlatformId = "x360" };
        document.Processor.HasSchemaIdOverride = true;
        document.Processor.SchemaId = "x360-mesh-shader-v1";
        document.Processor.FieldValues.Add("shader-asset-id", ShaderAssetIdUtils.BuildShaderAssetId(shaderOutput, assets));
        document.Processor.FieldValues.Add("vertex-program", "VS");
        document.Processor.FieldValues.Add("pixel-program", "PS");
        using var payload = new MemoryStream();
        MaterialAssetPlatformOverrideDocumentBinarySerializer.Serialize(payload, document);
        Directory.CreateDirectory(Path.GetDirectoryName(shaderOutput)!);
        using (var stream = new FileStream(shaderOutput, FileMode.CreateNew)) stream.Write(shader);
        using (var stream = new FileStream(output, FileMode.CreateNew)) stream.Write(payload.ToArray());
    }
}
