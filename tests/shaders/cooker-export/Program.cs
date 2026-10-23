using System.Security.Cryptography;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.x360.builder;

if (args.Length != 3) throw new ArgumentException("Expected output directory, Engine root and shaderc path.");
string root = Path.GetFullPath(args[0]);
string sourcePath = Path.Combine(Path.GetFullPath(args[1]), "engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl");
string text = File.ReadAllText(sourcePath);
var builder = new Xbox360PlatformAssetBuilder();
string cookRoot = Path.Combine(root, "cooked");
string meshPath = Path.GetFullPath("tests/shaders/mesh-material.hlsl");
string meshText = File.ReadAllText(meshPath);
var dependencies = builder.RendererShaderDependencies.Concat(new[] {
    new PlatformShaderDependency("mesh-material", "mesh.vs", "mesh.ps", "MeshMaterialV1")
}).ToArray();
var request = new PlatformShaderArtifactCookRequest(cookRoot, "x360", "release", "x360-unlit", dependencies,
    [new PlatformShaderArtifactCookSource("SpriteBatchShader", Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(text))), text, sourcePath),
     new PlatformShaderArtifactCookSource("mesh-material", Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(meshText))), meshText, meshPath)], helengine.NumericPrecisionPolicy.Default);
var result = new Xbox360ShaderArtifactCooker(new(shadercLibraryPath: args[2])).Cook(request);
var manifest = result.CookedArtifactDeclarations.Select(item => new PlatformBuildArtifact(item.RelativePath, item.LogicalArtifactId,
    Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(cookRoot, item.RelativePath)))) , item.ArtifactKind, item.VariantId)).ToArray();
var staged = new Xbox360ContentStager().Stage(manifest, cookRoot, Path.Combine(root, "staged"), CancellationToken.None);
if (staged.Count != 2) throw new InvalidOperationException("Expected the declared Textured and MeshMaterialV1 pairs.");
// The editor packages declared shader paths beneath cooked/, unlike direct cooker callers.
var editorManifest = manifest.Select(item => new PlatformBuildArtifact("cooked/" + item.RelativePath,
    item.LogicalArtifactId, item.ContentHash, item.ArtifactKind, item.VariantId)).ToArray();
new Xbox360ContentStager().Stage(editorManifest, root, Path.Combine(root, "archive-staged"), CancellationToken.None);
Console.WriteLine("Cooked and manifest-staged shared Textured and MeshMaterialV1 X3SP pairs.");
