using helengine.baseplatform.Manifest;

namespace helengine.x360.builder.tests;

/// <summary>Checks that a menu can package a real secondary scene without relaxing content validation.</summary>
public sealed class Xbox360SceneCompatibilityValidatorTests {
    /// <summary>Accepts menu and cube while preserving the menu startup route and both cooked catalog paths.</summary>
    [Fact]
    public void Packages_menu_and_cube_with_menu_startup() {
        PlatformBuildManifest manifest = Manifest([Scene("DemoDiscMainMenu"), Scene("cube_test")],
            [Artifact("DemoDiscMainMenu"), Artifact("cube_test")]);
        new Xbox360SceneCompatibilityValidator().Validate(manifest);
        string root = Path.Combine(GetOwnedTestRoot(), "menu-cube-catalog");
        string source = File.ReadAllText(new Xbox360StartupCatalogWriter().Write(root, manifest));
        Assert.Contains("{ \"DemoDiscMainMenu\", \"cooked/scenes/DemoDiscMainMenu.hasset\" }", source);
        Assert.Contains("{ \"cube_test\", \"cooked/scenes/cube_test.hasset\" }", source);
        Assert.Contains("kStartupSceneId[] = \"DemoDiscMainMenu\"", source);
        Assert.Contains("*count = 2", source);
    }

    /// <summary>Rejects a secondary scene when its cooked artifact was omitted instead of aliasing the menu.</summary>
    [Fact]
    public void Rejects_cube_without_its_cooked_artifact() {
        PlatformBuildManifest manifest = Manifest([Scene("DemoDiscMainMenu"), Scene("cube_test")], [Artifact("DemoDiscMainMenu")]);
        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            new Xbox360StartupCatalogWriter().Write(Path.Combine(GetOwnedTestRoot(), "missing-cube-catalog"), manifest));
        Assert.Contains("cube_test", error.Message);
        Assert.Contains("no cooked scene artifact", error.Message);
    }

    /// <summary>Rejects ambiguous logical ids before the catalog is staged.</summary>
    [Fact]
    public void Rejects_duplicate_scene_ids() {
        Assert.Throws<InvalidOperationException>(() => new Xbox360SceneCompatibilityValidator().Validate(
            Manifest([Scene("DemoDiscMainMenu"), Scene("demodiscmainmenu")], [])));
    }

    /// <summary>Rejects empty packages and packages without their declared boot scene.</summary>
    [Fact]
    public void Rejects_empty_or_missing_startup() {
        Assert.Throws<InvalidOperationException>(() => new Xbox360SceneCompatibilityValidator().Validate(Manifest([], [])));
        Assert.Throws<InvalidOperationException>(() => new Xbox360SceneCompatibilityValidator().Validate(Manifest([Scene("cube_test")], [])));
    }

    /// <summary>Retains renderer feature validation for a secondary scene in a multi-scene package.</summary>
    [Fact]
    public void Rejects_unsupported_cube_feature() {
        PlatformBuildRuntimeFeatureManifest features = new([new PlatformBuildRequiredRuntimeFeature("unsupported-test-feature", default, "cube_test", "Required by the secondary scene")]);
        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => new Xbox360SceneCompatibilityValidator().Validate(
            Manifest([Scene("DemoDiscMainMenu"), Scene("cube_test")], [], features)));
        Assert.Contains("unsupported-test-feature", error.Message);
        Assert.Contains("cube_test", error.Message);
    }

    /// <summary>Constructs one authored scene reference used by the package contract checks.</summary>
    static PlatformBuildScene Scene(string id) => new(id, id, "scenes/" + id + ".helen", [], []);

    /// <summary>Constructs one independently cooked scene artifact.</summary>
    static PlatformBuildArtifact Artifact(string id) => new("cooked/scenes/" + id + ".hasset", id, "sha256:test", "scene", "default");

    /// <summary>Constructs a package whose boot route always remains the real main menu.</summary>
    static PlatformBuildManifest Manifest(PlatformBuildScene[] scenes, PlatformBuildArtifact[] artifacts, PlatformBuildRuntimeFeatureManifest features = null) =>
        new(1, "demodisc", "1", "1", "x360", "1", "DemoDiscMainMenu", scenes, [], artifacts, [], [], new("default", []), [], features ?? PlatformBuildRuntimeFeatureManifest.Empty);

    /// <summary>Uses the caller-selected existing workspace output instead of the operating-system temporary directory.</summary>
    static string GetOwnedTestRoot() => Environment.GetEnvironmentVariable("HELENGINE_TEST_OUTPUT_ROOT")
        ?? throw new InvalidOperationException("HELENGINE_TEST_OUTPUT_ROOT must name the owned active test output.");
}
