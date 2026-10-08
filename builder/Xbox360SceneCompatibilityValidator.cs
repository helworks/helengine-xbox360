using helengine.baseplatform.Manifest;

namespace helengine.x360.builder;

/// <summary>
/// Rejects build manifests this target cannot run, before anything is staged or compiled.
/// </summary>
/// <remarks>
/// A scene that needs something the renderer does not implement should fail here, with the
/// feature named, rather than compile into an image that boots and quietly draws nothing.
/// The supported set grows as the renderer does.
/// </remarks>
public sealed class Xbox360SceneCompatibilityValidator {
    /// <summary>Runtime feature ids the Xbox 360 runtime implements today.</summary>
    static readonly HashSet<string> SupportedFeatures =
        new(StringComparer.OrdinalIgnoreCase) { "scene", "mesh", "camera", "material" };

    /// <summary>Throws when the manifest asks for more than this target can run.</summary>
    /// <param name="manifest">Cooked build manifest produced by the editor build graph.</param>
    public void Validate(PlatformBuildManifest manifest) {
        if (manifest == null) throw new ArgumentNullException(nameof(manifest));
        if (manifest.Scenes.Length == 0) {
            throw new InvalidOperationException("Xbox 360 builds require at least one packaged scene.");
        }
        HashSet<string> sceneIds = new(StringComparer.OrdinalIgnoreCase);
        foreach (PlatformBuildScene scene in manifest.Scenes) {
            if (!sceneIds.Add(scene.SceneId)) {
                throw new InvalidOperationException($"Two packaged scenes share the scene id '{scene.SceneId}'.");
            }
        }
        if (string.IsNullOrWhiteSpace(manifest.StartupSceneId) ||
            !manifest.Scenes.Any(scene => scene.SceneId == manifest.StartupSceneId)) {
            throw new InvalidOperationException($"The startup scene '{manifest.StartupSceneId}' is missing from the scene manifest.");
        }
        foreach (PlatformBuildRequiredRuntimeFeature feature in manifest.RuntimeFeatureManifest.RequiredFeatures) {
            if (!SupportedFeatures.Contains(feature.FeatureId)) {
                throw new InvalidOperationException(
                    $"The scene package needs runtime feature '{feature.FeatureId}' from '{feature.SourceId}', which the Xbox 360 renderer does not implement yet.");
            }
        }
    }
}
