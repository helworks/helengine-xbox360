using System.Text;
using helengine.baseplatform.Manifest;

namespace helengine.x360.builder;

/// <summary>
/// Writes the generated scene catalog the guest turns into the core's RuntimeSceneCatalog:
/// one table of scene id to cooked relative path, plus the scene to load at boot.
/// </summary>
/// <remarks>
/// This is the only way the console learns which cooked file is a scene. It is written beside
/// the generated core but compiled separately, so regenerating the core never overwrites it.
/// Duplicate ids, duplicate paths and a startup scene missing from the table are all rejected
/// before a file is written, because each of those becomes a boot-time failure with no
/// explanation on the target.
///
/// Only the source is generated. The declarations live in the player's own src/demo/scene_catalog.hpp,
/// which this includes, so the builder and the console cannot disagree about the table's shape:
/// a change to it is a compile error here rather than a scene that silently fails to load.
/// </remarks>
public sealed class Xbox360StartupCatalogWriter {
    /// <summary>Player header holding the declarations the generated source defines.</summary>
    public const string HeaderFileName = "scene_catalog.hpp";

    /// <summary>Source written next to the generated core.</summary>
    public const string SourceFileName = "xbox360_runtime_scene_catalog.cpp";

    /// <summary>Resolves each manifest scene to its cooked artifact and writes the catalog.</summary>
    /// <param name="outputRootPath">Directory receiving the catalog files.</param>
    /// <param name="manifest">Cooked build manifest.</param>
    /// <returns>Path of the written source.</returns>
    public string Write(string outputRootPath, PlatformBuildManifest manifest) {
        if (manifest == null) throw new ArgumentNullException(nameof(manifest));
        PlatformBuildArtifact[] sceneArtifacts = manifest.CookedArtifacts
            .Where(artifact => string.Equals(artifact.ArtifactKind, "scene", StringComparison.OrdinalIgnoreCase))
            .ToArray();

        KeyValuePair<string, string>[] entries = manifest.Scenes.Select(scene => {
            PlatformBuildArtifact artifact = sceneArtifacts.FirstOrDefault(
                candidate => string.Equals(candidate.LogicalArtifactId, scene.SceneId, StringComparison.Ordinal));
            if (artifact == null) {
                string identity = Xbox360ContentStager.NormalizeRelativePath(scene.SourceIdentity);
                artifact = sceneArtifacts.FirstOrDefault(
                    candidate => string.Equals(Xbox360ContentStager.NormalizeRelativePath(candidate.RelativePath), identity, StringComparison.Ordinal));
            }
            if (artifact == null && manifest.Scenes.Length == 1 && sceneArtifacts.Length == 1) {
                artifact = sceneArtifacts[0];
            }
            if (artifact == null) {
                throw new InvalidOperationException($"Scene '{scene.SceneId}' has no cooked scene artifact in the manifest.");
            }
            return new KeyValuePair<string, string>(scene.SceneId, Xbox360ContentStager.NormalizeRelativePath(artifact.RelativePath));
        }).ToArray();

        return Write(outputRootPath, manifest.StartupSceneId, entries);
    }

    /// <summary>Writes the catalog for explicit scene id to cooked path pairs.</summary>
    /// <param name="outputRootPath">Directory receiving the catalog files.</param>
    /// <param name="startupSceneId">Scene the guest loads at boot; must be one of the entries.</param>
    /// <param name="entries">Scene id to cooked relative path pairs.</param>
    /// <returns>Path of the written source.</returns>
    public string Write(string outputRootPath, string startupSceneId, IReadOnlyList<KeyValuePair<string, string>> entries) {
        if (string.IsNullOrWhiteSpace(outputRootPath)) throw new ArgumentException("An output root is required.", nameof(outputRootPath));
        if (string.IsNullOrWhiteSpace(startupSceneId)) throw new ArgumentException("A startup scene id is required.", nameof(startupSceneId));
        if (entries == null || entries.Count == 0) throw new InvalidOperationException("The scene catalog needs at least one scene.");
        if (!entries.Any(entry => string.Equals(entry.Key, startupSceneId, StringComparison.Ordinal))) {
            throw new InvalidOperationException($"The startup scene '{startupSceneId}' is missing from the catalog.");
        }
        HashSet<string> ids = new(StringComparer.Ordinal);
        HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
        foreach (KeyValuePair<string, string> entry in entries) {
            if (string.IsNullOrWhiteSpace(entry.Key)) throw new InvalidOperationException("A catalog entry has no scene id.");
            if (string.IsNullOrWhiteSpace(entry.Value)) throw new InvalidOperationException($"Scene '{entry.Key}' has no cooked path.");
            if (!ids.Add(entry.Key)) throw new InvalidOperationException($"Two catalog entries share the scene id '{entry.Key}'.");
            if (!paths.Add(entry.Value)) throw new InvalidOperationException($"Two catalog entries share the cooked path '{entry.Value}'.");
        }

        string root = Path.GetFullPath(outputRootPath);
        Directory.CreateDirectory(root);

        StringBuilder source = new();
        source.AppendLine("// Generated by helengine.x360.builder; do not edit.");
        source.AppendLine($"#include \"{HeaderFileName}\"");
        source.AppendLine();
        source.AppendLine("namespace {");
        source.AppendLine("const HEX360SceneCatalogEntry kEntries[] = {");
        foreach (KeyValuePair<string, string> entry in entries) {
            source.AppendLine($"    {{ {Quote(entry.Key)}, {Quote(entry.Value)} }},");
        }
        source.AppendLine("};");
        source.AppendLine($"const char kStartupSceneId[] = {Quote(startupSceneId)};");
        source.AppendLine("}");
        source.AppendLine();
        source.AppendLine("extern \"C\" const char* he_x360_startup_scene_id() { return kStartupSceneId; }");
        source.AppendLine();
        source.AppendLine("extern \"C\" const HEX360SceneCatalogEntry* he_x360_scene_catalog_entries(std::size_t* count) {");
        source.AppendLine("    if (count) {");
        source.AppendLine($"        *count = {entries.Count};");
        source.AppendLine("    }");
        source.AppendLine("    return kEntries;");
        source.AppendLine("}");

        string sourcePath = Path.Combine(root, SourceFileName);
        File.WriteAllText(sourcePath, source.ToString());
        return sourcePath;
    }

    /// <summary>Quotes a string for C++ source, escaping what would otherwise break the literal.</summary>
    static string Quote(string value) {
        StringBuilder quoted = new("\"");
        foreach (char character in value) {
            if (character == '\\' || character == '"') {
                quoted.Append('\\').Append(character);
            } else if (character >= ' ' && character <= '~') {
                quoted.Append(character);
            } else {
                throw new InvalidOperationException($"Scene catalog value '{value}' contains a character the generated source cannot carry.");
            }
        }
        return quoted.Append('"').ToString();
    }
}
