using helengine.baseplatform.Manifest;
using helengine.editor;
using System.Security.Cryptography;

namespace helengine.x360.builder;

/// <summary>Preflights and cooks native texture work items before the disc stager replaces existing content.</summary>
public sealed class Xbox360PlatformCookWorkItemExecutor {
    /// <summary>Converts authored files and generated font atlases into independent RGBA sources.</summary>
    readonly Xbox360SourceTextureDecoder Decoder = new();
    /// <summary>Produces native texture bytes from validated settings.</summary>
    readonly Xbox360TextureCooker Cooker = new();

    /// <summary>Validates the complete batch and prepares every payload before writing any cooked output.</summary>
    public IReadOnlyList<PlatformBuildArtifact> Execute(
        IReadOnlyList<PlatformCookWorkItem> workItems,
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        string packageSourceRootPath,
        CancellationToken cancellationToken) {
        return Execute(workItems, artifacts, packageSourceRootPath, RealRepresentation.Policy, cancellationToken);
    }

    /// <summary>Prepares all native textures with the selected target numeric envelope.</summary>
    public IReadOnlyList<PlatformBuildArtifact> Execute(
        IReadOnlyList<PlatformCookWorkItem> workItems,
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        string packageSourceRootPath,
        NumericPrecisionPolicy numericPolicy,
        CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(workItems);
        ArgumentNullException.ThrowIfNull(artifacts);
        if (string.IsNullOrWhiteSpace(packageSourceRootPath)) throw new ArgumentException("Package source root is required.", nameof(packageSourceRootPath));
        string root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(packageSourceRootPath));
        if (!Directory.Exists(root)) throw new DirectoryNotFoundException(root);
        ValidateNoLinks(root);
        cancellationToken.ThrowIfCancellationRequested();
        HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            if (artifact == null) throw new ArgumentException("Manifest artifacts cannot contain null entries.", nameof(artifacts));
            if (!paths.Add(Xbox360ContentStager.NormalizeRelativePath(artifact.RelativePath))) throw new InvalidOperationException("Duplicate manifest artifact path.");
        }
        HashSet<string> sourcePaths = new(StringComparer.OrdinalIgnoreCase);
        foreach (PlatformCookWorkItem item in workItems) {
            if (item == null) throw new ArgumentException("Cook work items cannot contain null entries.", nameof(workItems));
            sourcePaths.Add(Path.GetFullPath(item.SourceAssetPath));
        }
        List<Xbox360TextureCookWorkPlan> plans = new(workItems.Count);
        foreach (PlatformCookWorkItem item in workItems) {
            cancellationToken.ThrowIfCancellationRequested();
            if (!string.Equals(item.TargetPlatformId, "x360", StringComparison.OrdinalIgnoreCase) || item.TargetArtifactKind != "runtime-texture"
                || (item.SourceAssetKind != "texture" && item.SourceAssetKind != "font-atlas-texture")) {
                throw new InvalidOperationException($"Unsupported Xbox360 cook contract for '{item.WorkItemId}'.");
            }
            TextureAssetProcessorSettings settings = Xbox360TextureCookSettingsSerializer.Deserialize(item.SerializedPlatformSettings);
            string relative = Xbox360ContentStager.NormalizeRelativePath(item.OutputRelativePath);
            foreach (string existingPath in paths) {
                if (relative.StartsWith(existingPath + "/", StringComparison.OrdinalIgnoreCase) || existingPath.StartsWith(relative + "/", StringComparison.OrdinalIgnoreCase)) {
                    throw new InvalidOperationException($"Xbox360 cooked output '{relative}' conflicts with file/directory path '{existingPath}'.");
                }
            }
            if (!paths.Add(relative)) throw new InvalidOperationException($"Duplicate Xbox360 cooked output '{relative}'.");
            string destination = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
            if (!destination.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Xbox360 cooked output escapes the package source root.");
            ValidateNoLinks(destination);
            ValidateFileDestination(destination, root);
            if (sourcePaths.Contains(destination)) throw new InvalidOperationException("Xbox360 cooked output would replace a cook source.");
            if (!File.Exists(item.SourceAssetPath)) throw new FileNotFoundException("Xbox360 texture source was not found.", item.SourceAssetPath);
            plans.Add(new Xbox360TextureCookWorkPlan { WorkItem = item, Settings = settings, RelativePath = relative, DestinationPath = destination });
        }
        foreach (Xbox360TextureCookWorkPlan plan in plans) {
            cancellationToken.ThrowIfCancellationRequested();
            using TextureAsset source = Decoder.Decode(plan.WorkItem.SourceAssetPath);
            using TextureAsset cooked = Cooker.Cook(source, plan.Settings);
            cooked.Id = plan.WorkItem.OutputLogicalArtifactId;
            cooked.RuntimeAssetId = RuntimeAssetIdGenerator.Generate(cooked.Id);
            plan.Payload = Xbox360CookedTextureSerializer.SerializeToBytes(cooked, numericPolicy);
        }
        cancellationToken.ThrowIfCancellationRequested();
        foreach (Xbox360TextureCookWorkPlan plan in plans) {
            // Check again after cooking so a replaced directory cannot redirect publication through a junction.
            ValidateNoLinks(plan.DestinationPath);
        }
        foreach (Xbox360TextureCookWorkPlan plan in plans) {
            Directory.CreateDirectory(Path.GetDirectoryName(plan.DestinationPath));
            File.WriteAllBytes(plan.DestinationPath, plan.Payload);
        }
        return plans.Select(plan => new PlatformBuildArtifact(plan.RelativePath, plan.WorkItem.OutputLogicalArtifactId,
            "sha256:" + Convert.ToHexString(SHA256.HashData(plan.Payload)), "runtime-texture", "x360")).ToArray();
    }

    /// <summary>Rejects links in existing ancestors so a contained spelling cannot write outside the workspace root.</summary>
    static void ValidateNoLinks(string path) {
        string current = Path.GetFullPath(path);
        while (!string.IsNullOrEmpty(current)) {
            if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) {
                throw new InvalidOperationException($"Xbox360 cooked output traverses a link at '{current}'.");
            }
            current = Path.GetDirectoryName(current);
        }
    }

    /// <summary>Preflights directory/file conflicts instead of discovering them after publishing an earlier output.</summary>
    static void ValidateFileDestination(string destination, string root) {
        if (Directory.Exists(destination)) throw new InvalidOperationException($"Xbox360 cooked output destination is a directory: '{destination}'.");
        string current = Path.GetDirectoryName(destination);
        while (current != null && !string.Equals(current, root, StringComparison.OrdinalIgnoreCase)) {
            if (File.Exists(current)) throw new InvalidOperationException($"Xbox360 cooked output ancestor is a file: '{current}'.");
            current = Path.GetDirectoryName(current);
        }
    }
}
