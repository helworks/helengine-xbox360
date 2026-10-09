using helengine;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Targets;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

/// <summary>Verifies the public builder executes work items before staging or invoking the native compiler.</summary>
public sealed class Xbox360TextureBuildIntegrationTests : IDisposable {
    /// <summary>Contains this class's fixed fixtures under dotnet's configured output root.</summary>
    readonly string Root = Path.Combine(AppContext.BaseDirectory, "texture-build-integration");
    /// <summary>Gets the builder working directory containing editor-packaged sources.</summary>
    string WorkingRoot => Path.Combine(Root, "working");

    /// <summary>Creates a serialized font atlas source without invoking an image decoder or native toolchain.</summary>
    public Xbox360TextureBuildIntegrationTests() {
        Directory.CreateDirectory(Path.Combine(WorkingRoot, "package-source"));
        Directory.CreateDirectory(Path.Combine(Root, "generated-core"));
        File.WriteAllText(Path.Combine(WorkingRoot, "package-source", "menu.heasset"), "test-scene-payload");
        using TextureAsset source = Xbox360TextureCookTests.Source(4, 2);
        File.WriteAllBytes(Path.Combine(Root, "atlas.heasset"), helengine.files.AssetSerializer.SerializeToBytes(source));
    }

    /// <summary>Requires the native executor to observe the successfully cooked native font atlas in the staged disc.</summary>
    [Fact]
    public async Task BuildCooksTextureWorkItemBeforeNativeInvocation() {
        TextureRecordingNativeBuildExecutor native = new();
        var report = await new Xbox360PlatformAssetBuilder(native).BuildAsync(Request("fonts/atlas.heasset"), new TextureBuildReporter(), new TextureBuildReporter(), CancellationToken.None);
        Assert.True(report.Succeeded, string.Join(Environment.NewLine, report.Diagnostics.Select(item => item.Message)));
        Assert.Equal(1, native.Calls);
        Assert.Equal(TextureAssetColorFormat.Xbox360Native, native.StagedTextureFormat);
    }

    /// <summary>Requires a rejected cook to leave the existing disc unchanged and avoid the native toolchain.</summary>
    [Fact]
    public async Task InvalidCookPreservesExistingDiscAndSkipsNativeInvocation() {
        string stage = Path.Combine(WorkingRoot, "x360-content");
        Directory.CreateDirectory(stage);
        File.WriteAllText(Path.Combine(stage, "previous.txt"), "previous");
        TextureRecordingNativeBuildExecutor native = new();
        var report = await new Xbox360PlatformAssetBuilder(native).BuildAsync(Request("../outside.heasset"), new TextureBuildReporter(), new TextureBuildReporter(), CancellationToken.None);
        Assert.False(report.Succeeded);
        Assert.Equal(0, native.Calls);
        Assert.Equal("previous", File.ReadAllText(Path.Combine(stage, "previous.txt")));
        Assert.Contains(report.Diagnostics, item => item.Code == "X360BUILD_FAILED");
    }

    /// <summary>Constructs a complete editor-shaped request with one startup scene and one font-atlas cook.</summary>
    PlatformBuildRequest Request(string outputPath) {
        PlatformCookWorkItem item = new("atlas", Path.Combine(Root, "atlas.heasset"), "font-atlas-texture", "x360", "runtime-texture", outputPath,
            "fonts/atlas", "sourcehash", "settingshash", Xbox360TextureCookSettingsSerializer.Serialize(Xbox360TextureCookSettingsSerializer.CreateDefaults()), []);
        PlatformBuildManifest manifest = new(1, "project", "1.0.0", "1.0.0", "x360", "1.0.0", "menu",
            [new PlatformBuildScene("menu", "Menu", "scenes/menu.helen", [], [])], [], [new PlatformBuildArtifact("menu.heasset", "menu", "sourcehash", "scene", "x360")], [], [], new PlatformContainerWritePlan("x360", []), [item], PlatformBuildRuntimeFeatureManifest.Empty);
        return new PlatformBuildRequest(manifest, [new PlatformBuildTargetVariant("release", "x360", "x360", "release")],
            [new PlatformCookProfile("release", "release", new PlatformCookProfileCapabilities("x360", "x360-standard", "pcm-buffered", "x360-scene-v1", PlatformSerializationEndianness.BigEndian))],
            Path.Combine(Root, "output"), WorkingRoot, "release", "x360-standard", "default", new Dictionary<string, string>(), new Dictionary<string, string>(), new Dictionary<string, string>(), Path.Combine(Root, "generated-core"));
    }

    /// <summary>Removes only the fixture directory verified beneath this test assembly's output.</summary>
    public void Dispose() {
        string full = Path.GetFullPath(Root);
        if (!full.StartsWith(Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory)) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Fixture root escaped test output.");
        if (Directory.Exists(full)) Directory.Delete(full, true);
    }
}
