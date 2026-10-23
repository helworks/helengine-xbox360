using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.x360.builder;

/// <summary>
/// Platform discovery and build contract for the Xbox 360 target.
/// </summary>
/// <remarks>
/// The editor build graph cooks assets and regenerates the core. This builder checks the
/// manifest is something the renderer can actually run, stages only the files the manifest
/// declares, writes the scene catalog the console reads at boot, and runs the native build
/// that compiles the core and links the image.
/// </remarks>
public sealed partial class Xbox360PlatformAssetBuilder : IPlatformAssetBuilder, IPlatformShaderArtifactBuilder, IPlatformRendererShaderDependencyProvider {
    /// <summary>The image the native build must produce for a build to count as successful.</summary>
    public const string ImageFileName = "helengine.xex";

    /// <summary>Cooks the opaque unlit material subset.</summary>
    readonly Xbox360MaterialCooker MaterialCooker = new();

    /// <summary>Runs the native toolchain; replaceable so tests need no cross compiler.</summary>
    readonly IXbox360NativeBuildExecutor NativeBuildExecutor;

    /// <summary>Creates the builder the editor discovers through its parameterless constructor.</summary>
    public Xbox360PlatformAssetBuilder()
        : this(new Xbox360NativeBuildExecutor()) {
    }

    /// <summary>Creates the builder with an explicit native build executor.</summary>
    /// <param name="nativeBuildExecutor">Native build runner.</param>
    public Xbox360PlatformAssetBuilder(IXbox360NativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? throw new ArgumentNullException(nameof(nativeBuildExecutor));
        Descriptor = new PlatformBuilderDescriptor(
            "helengine.x360.builder",
            "0.1.0",
            "x360",
            new EngineCompatibilityRange("1.0.0", "999.0.0"),
            new ManifestCompatibilityRange(1, 1),
            ["x360"],
            ["x360"]);
        Definition = Xbox360PlatformDefinitionFactory.Create();
    }

    /// <inheritdoc />
    public PlatformBuilderDescriptor Descriptor { get; }

    /// <inheritdoc />
    public PlatformDefinition Definition { get; }

    public IReadOnlyList<PlatformShaderDependency> RendererShaderDependencies =>
        Array.AsReadOnly(new[] { new PlatformShaderDependency("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured") });

    public PlatformShaderArtifactCookResult CookShaderArtifacts(PlatformShaderArtifactCookRequest request) =>
        new Xbox360ShaderArtifactCooker().Cook(request);

    /// <inheritdoc />
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) => MaterialCooker.Cook(request);

    /// <inheritdoc />
    public Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        CancellationToken cancellationToken) {
        if (request == null) throw new ArgumentNullException(nameof(request));
        if (progressReporter == null) throw new ArgumentNullException(nameof(progressReporter));
        if (diagnosticReporter == null) throw new ArgumentNullException(nameof(diagnosticReporter));

        List<PlatformBuildDiagnostic> diagnostics = [];
        string stagingRoot = Path.Combine(request.WorkingRoot, "x360-content");
        string catalogRoot = Path.Combine(request.WorkingRoot, "x360-catalog");
        string imagePath = Path.Combine(request.OutputRoot, ImageFileName);
        try {
            cancellationToken.ThrowIfCancellationRequested();
            Directory.CreateDirectory(request.OutputRoot);
            // A previous image must never be mistaken for this build's output.
            if (File.Exists(imagePath)) {
                File.Delete(imagePath);
            }
            if (!string.Equals(request.Manifest.PlatformName, "x360", StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"The Xbox 360 builder received a manifest for platform '{request.Manifest.PlatformName}'.");
            }
            if (string.IsNullOrWhiteSpace(request.GeneratedCoreCppRootPath) || !Directory.Exists(request.GeneratedCoreCppRootPath)) {
                throw new DirectoryNotFoundException($"The Xbox 360 builder needs the generated core root; '{request.GeneratedCoreCppRootPath}' was not found.");
            }

            new Xbox360SceneCompatibilityValidator().Validate(request.Manifest);

            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Content", "manifest", 0, 1, "Staging declared cooked artifacts."));
            string packageSourceRoot = ResolvePackageSourceRoot(request.WorkingRoot);
            IReadOnlyList<PlatformBuildArtifact> builderCookedArtifacts = new Xbox360PlatformCookWorkItemExecutor().Execute(
                request.Manifest.PlatformCookWorkItems, request.Manifest.CookedArtifacts, packageSourceRoot, request.NumericPolicy, cancellationToken);
            IReadOnlyList<Xbox360StagedAsset> staged = new Xbox360ContentStager()
                .Stage(request.Manifest.CookedArtifacts, builderCookedArtifacts, packageSourceRoot, stagingRoot, cancellationToken);
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Content", "manifest", 1, 1, $"Staged {staged.Count} cooked file(s)."));

            new Xbox360StartupCatalogWriter().Write(catalogRoot, request.Manifest);

            progressReporter.Report(new PlatformBuildProgressUpdate("Build Image", ImageFileName, 0, 1, "Compiling the generated core and linking the image."));
            string nativeCacheRoot = string.IsNullOrWhiteSpace(request.NativeObjectCacheRoot)
                ? Path.Combine(request.WorkingRoot, "x360-native-cache")
                : request.NativeObjectCacheRoot;
            NativeBuildExecutor.Build(request.GeneratedCoreCppRootPath, stagingRoot, catalogRoot, request.OutputRoot, nativeCacheRoot, cancellationToken);
            progressReporter.Report(new PlatformBuildProgressUpdate("Build Image", ImageFileName, 1, 1, "Linked the image."));
        } catch (OperationCanceledException) {
            throw;
        } catch (Exception exception) {
            PlatformBuildDiagnostic diagnostic = new(
                PlatformBuildDiagnosticSeverity.Error,
                "X360BUILD_FAILED",
                exception.Message,
                request.Manifest.StartupSceneId,
                string.Empty,
                "x360-builder");
            diagnostics.Add(diagnostic);
            diagnosticReporter.Report(diagnostic);
        }

        bool succeeded = diagnostics.Count == 0 && File.Exists(imagePath);
        PlatformBuildItemOutcomeKind outcome = succeeded ? PlatformBuildItemOutcomeKind.Succeeded : PlatformBuildItemOutcomeKind.Failed;
        return Task.FromResult(new PlatformBuildReport(
            succeeded,
            [.. diagnostics],
            request.Manifest.Scenes.Select(scene => new PlatformBuildItemOutcome(scene.SceneId, outcome)).ToArray(),
            request.Manifest.LooseAssets.Select(asset => new PlatformBuildItemOutcome(asset.AssetId, outcome)).ToArray()));
    }

    /// <summary>Locates the editor-staged package-source tree that owns the cooked files.</summary>
    static string ResolvePackageSourceRoot(string workingRootPath) {
        if (string.IsNullOrWhiteSpace(workingRootPath)) {
            throw new ArgumentException("A builder working root is required.", nameof(workingRootPath));
        }
        string packageSourceRoot = Path.Combine(Path.GetFullPath(workingRootPath), "package-source");
        if (!Directory.Exists(packageSourceRoot)) {
            throw new DirectoryNotFoundException($"The editor's package source tree was not found at '{packageSourceRoot}'.");
        }
        return packageSourceRoot;
    }
}
