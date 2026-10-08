using helengine.baseplatform.Manifest;
using System.Security.Cryptography;

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

        cancellationToken.ThrowIfCancellationRequested();
        string sourceRoot = Path.GetFullPath(sourceRootPath);
        string stagingRoot = Path.GetFullPath(stagingRootPath);
        ValidateNoReparsePoints(sourceRoot);
        ValidateNoReparsePoints(stagingRoot);
        if (IsSameOrDescendant(sourceRoot, stagingRoot) || IsSameOrDescendant(stagingRoot, sourceRoot)) {
            throw new InvalidOperationException("The source and staging roots must not overlap.");
        }

        HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
        List<(string RelativePath, string SourcePath, string DestinationPath)> prepared = new(artifacts.Count);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            if (artifact == null) throw new InvalidOperationException("The cooked artifact manifest contains a null entry.");
            string relativePath = NormalizeRelativePath(artifact.RelativePath);
            if (!paths.Add(relativePath)) {
                throw new InvalidOperationException($"Two cooked artifacts claim the same path: '{relativePath}'.");
            }
            string sourcePath = ResolveUnderRoot(sourceRoot, relativePath);
            if (!File.Exists(sourcePath)) {
                throw new FileNotFoundException($"Cooked artifact '{relativePath}' was not found under the package source root '{sourceRoot}'.", sourcePath);
            }
            string destinationPath = ResolveUnderRoot(stagingRoot, relativePath);
            ValidateNoReparsePoints(sourcePath);
            ValidateContentHash(artifact, sourcePath);
            prepared.Add((relativePath, sourcePath, destinationPath));
        }

        cancellationToken.ThrowIfCancellationRequested();
        // Keep the preceding build intact when any manifest declaration fails preflight.
        // A valid build still clears undeclared files before packing its fresh content.
        if (Directory.Exists(stagingRoot)) {
            Directory.Delete(stagingRoot, true);
        }
        Directory.CreateDirectory(stagingRoot);
        List<Xbox360StagedAsset> staged = new(prepared.Count);
        foreach ((string relativePath, string sourcePath, string destinationPath) in prepared) {
            cancellationToken.ThrowIfCancellationRequested();
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath) ?? stagingRoot);
            File.Copy(sourcePath, destinationPath, true);
            staged.Add(new Xbox360StagedAsset(relativePath, destinationPath));
        }

        staged.Sort((left, right) => string.CompareOrdinal(left.RelativePath, right.RelativePath));
        return staged;
    }

    /// <summary>Normalizes slash direction and rejects rooted paths and ambiguous runtime path segments.</summary>
    internal static string NormalizeRelativePath(string relativePath) {
        if (string.IsNullOrWhiteSpace(relativePath)) {
            throw new InvalidOperationException("A cooked artifact has no relative path.");
        }
        string slashNormalized = relativePath.Replace('\\', '/');
        if (slashNormalized.Split('/').Any(segment => segment.Contains(':') || segment.EndsWith('.') || segment.EndsWith(' '))) {
            throw new InvalidOperationException($"Cooked artifact path '{relativePath}' contains a Windows path alias.");
        }
        string normalized = slashNormalized.Trim();
        _ = helengine.CanonicalPackagedAssetPath.Normalize(normalized);
        return string.Join('/', normalized.Split('/', StringSplitOptions.RemoveEmptyEntries));
    }

    /// <summary>Verifies current editor SHA-256 receipts while preserving legacy opaque content identities.</summary>
    static void ValidateContentHash(PlatformBuildArtifact artifact, string sourcePath) {
        if (!artifact.ContentHash.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase)) return;
        using FileStream source = File.OpenRead(sourcePath);
        string actual = "sha256:" + Convert.ToHexString(SHA256.HashData(source));
        if (!string.Equals(actual, artifact.ContentHash, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Cooked artifact '{artifact.RelativePath}' does not match its declared SHA-256 content hash.");
        }
    }

    /// <summary>Checks directory ancestry at a separator boundary, including equality and filesystem roots.</summary>
    static bool IsSameOrDescendant(string path, string parent) {
        string relative = Path.GetRelativePath(parent, path);
        return !Path.IsPathRooted(relative) && relative != ".."
            && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal);
    }

    /// <summary>Refuses filesystem links that can disguise overlapping roots or redirect a declared source outside its root.</summary>
    /// <param name="path">Source file, root, or destination root whose existing ancestors must be ordinary directories.</param>
    static void ValidateNoReparsePoints(string path) {
        string current = path;
        while (!string.IsNullOrEmpty(current)) {
            if ((File.Exists(current) || Directory.Exists(current))
                && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) {
                throw new InvalidOperationException($"Content staging path '{path}' crosses filesystem link '{current}'.");
            }
            current = Path.GetDirectoryName(current);
        }
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
