using helengine.baseplatform.Manifest;

namespace helengine.x360.builder;

/// <summary>
/// Copies only manifest-declared cooked files into a fresh staging root, which
/// tools/pack-content.py then packs into the archive linked inside the image.
/// </summary>
/// <remarks>
/// Only declared files are staged. The packer embeds whatever it finds, and an image has no
/// room for anything nobody asked for, so an undeclared file left lying in the package source
/// must not reach the console by accident.
/// </remarks>
public sealed class Xbox360ContentStager {
    /// <summary>Stages every declared artifact and returns the staged list sorted by path.</summary>
    /// <param name="artifacts">Cooked artifacts declared by the build manifest.</param>
    /// <param name="sourceRootPath">Editor package-source root that owns the cooked files.</param>
    /// <param name="stagingRootPath">Fresh staging root; any previous content is removed.</param>
    /// <param name="cancellationToken">Cancellation token for the copy loop.</param>
    /// <returns>Staged assets keyed by the relative path the runtime will request.</returns>
    public IReadOnlyList<Xbox360StagedAsset> Stage(
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        string sourceRootPath,
        string stagingRootPath,
        CancellationToken cancellationToken) {
        if (artifacts == null) throw new ArgumentNullException(nameof(artifacts));
        if (string.IsNullOrWhiteSpace(sourceRootPath)) throw new ArgumentException("Source root is required.", nameof(sourceRootPath));
        if (string.IsNullOrWhiteSpace(stagingRootPath)) throw new ArgumentException("Staging root is required.", nameof(stagingRootPath));

        string sourceRoot = Path.GetFullPath(sourceRootPath);
        string stagingRoot = Path.GetFullPath(stagingRootPath);
        if (string.Equals(TrimSeparators(sourceRoot), TrimSeparators(stagingRoot), StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("The source and staging roots must be different directories.");
        }
        if (Directory.Exists(stagingRoot)) {
            Directory.Delete(stagingRoot, true);
        }
        Directory.CreateDirectory(stagingRoot);

        HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
        List<Xbox360StagedAsset> staged = new(artifacts.Count);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            string relativePath = NormalizeRelativePath(artifact.RelativePath);
            if (!paths.Add(relativePath)) {
                throw new InvalidOperationException($"Two cooked artifacts claim the same path: '{relativePath}'.");
            }
            string sourcePath = ResolveUnderRoot(sourceRoot, relativePath);
            if (!File.Exists(sourcePath)) {
                throw new FileNotFoundException($"Cooked artifact '{relativePath}' was not found under the package source root '{sourceRoot}'.", sourcePath);
            }
            string destinationPath = ResolveUnderRoot(stagingRoot, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath) ?? stagingRoot);
            File.Copy(sourcePath, destinationPath, true);
            staged.Add(new Xbox360StagedAsset(relativePath, destinationPath));
        }

        staged.Sort((left, right) => string.CompareOrdinal(left.RelativePath, right.RelativePath));
        return staged;
    }

    /// <summary>Canonicalizes a manifest path to forward slashes with no leading separator.</summary>
    internal static string NormalizeRelativePath(string relativePath) {
        if (string.IsNullOrWhiteSpace(relativePath)) {
            throw new InvalidOperationException("A cooked artifact has no relative path.");
        }
        string normalized = relativePath.Replace('\\', '/').Trim();
        while (normalized.StartsWith('/')) {
            normalized = normalized[1..];
        }
        if (normalized.Length == 0) {
            throw new InvalidOperationException($"Cooked artifact path '{relativePath}' is empty after normalization.");
        }
        return normalized;
    }

    /// <summary>Resolves a relative path under a root and refuses anything that escapes it.</summary>
    static string ResolveUnderRoot(string rootPath, string relativePath) {
        string combined = Path.GetFullPath(Path.Combine(rootPath, relativePath));
        string root = TrimSeparators(Path.GetFullPath(rootPath));
        if (!combined.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Cooked artifact path '{relativePath}' escapes its root.");
        }
        return combined;
    }

    /// <summary>Removes trailing directory separators so two roots compare equal.</summary>
    static string TrimSeparators(string path) =>
        path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
}
