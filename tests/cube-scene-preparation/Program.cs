using helengine.editor;

try {
string root = Path.Combine(Path.GetTempPath(), "x360-cube-preparation-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
string material = Path.Combine(root, "assets/materials/rendering/cube_test/CubeTestSolid.hasset");
Directory.CreateDirectory(Path.GetDirectoryName(material)!);
File.WriteAllText(material, "original material sentinel");
string scene = Path.Combine(root, "assets/scenes/rendering/cube_test.helen");
Directory.CreateDirectory(Path.GetDirectoryName(scene)!);
File.WriteAllText(scene, "original scene sentinel");
string shader = Path.GetFullPath("tests/shaders/mesh-material.hlsl");
CubeTestShaderPreparation.Prepare(root, shader);
if (File.ReadAllText(material) != "original material sentinel" || File.ReadAllText(scene) != "original scene sentinel")
    throw new Exception("Preparation changed authored scene or common material.");
using var stream = File.OpenRead(material + ".x360.hasset");
var document = MaterialAssetPlatformOverrideDocumentBinarySerializer.Deserialize(stream);
if (document.PlatformId != "x360" || !document.Processor.HasSchemaIdOverride || document.Processor.SchemaId != "x360-mesh-shader-v1")
    throw new Exception("Missing Xbox 360 shader schema override.");
var fields = document.Processor.FieldValues;
if (fields["shader-asset-id"] != "shaders.X360CubeMesh" || fields["vertex-program"] != "VS" || fields["pixel-program"] != "PS")
    throw new Exception("Wrong shader source identity or entrypoints.");
if (File.ReadAllText(Path.Combine(root, "assets/shaders/X360CubeMesh.hlsl")) != File.ReadAllText(shader))
    throw new Exception("Shader source changed during preparation.");
bool rejected = false;
try { CubeTestShaderPreparation.Prepare(root, shader); } catch (IOException) { rejected = true; }
if (!rejected) throw new Exception("Existing override must not be overwritten.");
Console.WriteLine("PASS: material override, unchanged scene/common material, exact shader source, overwrite rejection.");
Console.WriteLine("Retained fixture: " + root);
if (args.Length == 1) {
    var config = new EditorBuildConfigService(args[0]).TryLoadExisting();
    var platform = config.Platforms.Single(item => item.PlatformId == "x360");
    if (!platform.OverridesProjectScenes || platform.SelectedSceneIds.Count != 1
        || platform.SelectedSceneIds[0] != "cube_test")
        throw new Exception($"Expected cube_test only; override={platform.OverridesProjectScenes}, scenes=[{string.Join(", ", platform.SelectedSceneIds)}].");
    Console.WriteLine("PASS: real editor config resolves only runtime scene cube_test.");
}
return 0;
} catch (Exception exception) {
    Console.Error.WriteLine(exception);
    return 1;
}
