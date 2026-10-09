using helengine;
using helengine.baseplatform.Manifest;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

/// <summary>Checks batch preflight protects existing outputs and stages cooked textures and font atlases.</summary>
public sealed class Xbox360TextureWorkItemTests : IDisposable {
    /// <summary>Uses one fixed test root under the explicit dotnet artifacts directory.</summary>
    readonly string Root = Path.Combine(AppContext.BaseDirectory, "texture-work-items");
    /// <summary>Gets the contained package-source tree consumed by staging.</summary>
    string Package => Path.Combine(Root, "package-source");
    /// <summary>Gets the engine texture source retained untouched by the cook.</summary>
    string Source => Path.Combine(Root, "source.heasset");

    /// <summary>Creates a source texture and an existing package output to detect partial writes.</summary>
    public Xbox360TextureWorkItemTests() {
        Directory.CreateDirectory(Package);
        using TextureAsset source = Xbox360TextureCookTests.Source(4, 2);
        File.WriteAllBytes(Source, helengine.files.AssetSerializer.SerializeToBytes(source));
        File.WriteAllText(Path.Combine(Package, "previous.txt"), "previous");
    }

    /// <summary>Checks both source kinds reach native payloads and then the safe disc stager.</summary>
    [Fact]
    public void TextureAndFontAtlasWorkItemsCookBeforeStaging() {
        var outputs = new Xbox360PlatformCookWorkItemExecutor().Execute(
            [Item("textures/image.heasset"), Item("fonts/atlas.heasset", "font-atlas-texture")], [], Package, CancellationToken.None);
        string stage = Path.Combine(Root, "disc");
        Assert.Equal(outputs.Select(item => item.RelativePath).OrderBy(path => path, StringComparer.Ordinal), new Xbox360ContentStager().Stage([], outputs, Package, stage, CancellationToken.None).Select(item => item.RelativePath));
        foreach (var output in outputs) {
            string path = output.RelativePath;
            using FileStream stagedFile = File.OpenRead(Path.Combine(stage, path));
            Assert.Equal(EngineBinaryEndianness.BigEndian, EngineBinaryHeaderSerializer.Read(stagedFile).Endianness);
            using TextureAsset cooked = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(Path.Combine(stage, path))));
            Assert.Equal(TextureAssetColorFormat.Xbox360Native, cooked.ColorFormat);
            Assert.Equal(path, cooked.Id);
            Assert.NotEqual(0ul, cooked.RuntimeAssetId);
            Assert.Equal(4 * 2 * 4, TextureAssetPixelCodec.DecodeToRgba32(cooked).Length);
        }
    }

    /// <summary>Rejects invalid later output paths before an earlier valid work item can write.</summary>
    [Theory]
    [InlineData("../outside.heasset")]
    [InlineData("C:/outside.heasset")]
    [InlineData("image.heasset:stream")]
    public void TraversalOrRootedOutputPreflightWritesNothing(string path) {
        Assert.Throws<InvalidOperationException>(() => new Xbox360PlatformCookWorkItemExecutor().Execute([Item("first.heasset"), Item(path)], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "first.heasset")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Checks builder outputs and manifest artifacts are mutually exclusive after normalization.</summary>
    [Fact]
    public void ManifestCollisionFailsBeforeCooking() {
        Assert.Throws<InvalidOperationException>(() => new Xbox360PlatformCookWorkItemExecutor().Execute([Item("TEXTURES/image.heasset")],
            [new PlatformBuildArtifact("textures/image.heasset", "hash", "texture", "default")], Package, CancellationToken.None));
        Assert.False(Directory.Exists(Path.Combine(Package, "TEXTURES")));
    }

    /// <summary>Checks a later decode failure cannot publish the earlier successfully cooked payload.</summary>
    [Fact]
    public void LaterInvalidSourcePreservesPackageOutputs() {
        string bad = Path.Combine(Root, "bad-source.heasset");
        File.WriteAllBytes(bad, [1, 2, 3]);
        Assert.ThrowsAny<Exception>(() => new Xbox360PlatformCookWorkItemExecutor().Execute([Item("first.heasset"), Item("second.heasset", source: bad)], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "first.heasset")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Rejects a file/directory output collision before any prepared payload is published.</summary>
    [Fact]
    public void ParentChildOutputsFailPreflight() {
        Assert.Throws<InvalidOperationException>(() => new Xbox360PlatformCookWorkItemExecutor().Execute([Item("texture"), Item("texture/child.heasset")], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "texture")));
    }

    /// <summary>Rejects existing file ancestors without modifying unrelated prior outputs.</summary>
    [Fact]
    public void ExistingFileAncestorFailsPreflight() {
        Assert.Throws<InvalidOperationException>(() => new Xbox360PlatformCookWorkItemExecutor().Execute([Item("first.heasset"), Item("previous.txt/child.heasset")], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "first.heasset")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Stages legacy atlas settings without replacing their authored generic format with the new default.</summary>
    [Fact]
    public void LegacyFontAtlasSettingsRemainGenericThroughStaging() {
        var settings = Xbox360TextureCookSettingsSerializer.CreateDefaults();
        settings.ColorFormatId = "Indexed4";
        settings.IndexingMethodId = "QuantizedIndexed";
        var item = new PlatformCookWorkItem("atlas", Source, "font-atlas-texture", "x360", "runtime-texture", "fonts/legacy.heasset", "fonts/legacy", "sourcehash", "settingshash", Xbox360TextureCookSettingsSerializer.Serialize(settings), []);
        var paths = new Xbox360PlatformCookWorkItemExecutor().Execute([item], [], Package, CancellationToken.None);
        string stage = Path.Combine(Root, "disc");
        new Xbox360ContentStager().Stage([], paths, Package, stage, CancellationToken.None);
        using TextureAsset cooked = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(Path.Combine(stage, paths[0].RelativePath))));
        Assert.Equal(TextureAssetColorFormat.Indexed4, cooked.ColorFormat);
        using TextureAsset original = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(Source)));
        Assert.Equal(original.Colors, TextureAssetPixelCodec.DecodeToRgba32(cooked));
    }

    /// <summary>Checks the stager validates cooked receipts instead of accepting stale bytes at a valid path.</summary>
    [Fact]
    public void CookedOutputHashMismatchPreservesPreviousStage() {
        var outputs = new Xbox360PlatformCookWorkItemExecutor().Execute([Item("image.heasset")], [], Package, CancellationToken.None);
        File.AppendAllText(Path.Combine(Package, outputs[0].RelativePath), "corruption");
        string stage = Path.Combine(Root, "disc");
        Directory.CreateDirectory(stage);
        File.WriteAllText(Path.Combine(stage, "previous.txt"), "previous");
        Assert.Throws<InvalidOperationException>(() => new Xbox360ContentStager().Stage([], outputs, Package, stage, CancellationToken.None));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(stage, "previous.txt")));
    }

    /// <summary>Preserves both intermediate two-bit alpha levels through source decoding, cooking, BE serialization, staging and preview.</summary>
    [Theory]
    [InlineData(7)]
    [InlineData(54)]
    public void NativeTwoBitAlphaSurvivesCookAndStaging(int hardwareFormat) {
        Assert.True(Xbox360NativeTextureFormatCatalog.TryGetHardwareFormat(hardwareFormat, out var format));
        using TextureAsset source = Xbox360TextureCookTests.Source(2, 1);
        source.Colors[3] = 85;
        source.Colors[7] = 170;
        byte[] original = (byte[])source.Colors.Clone();
        File.WriteAllBytes(Source, helengine.files.AssetSerializer.SerializeToBytes(source));
        var settings = Xbox360TextureCookSettingsSerializer.CreateDefaults();
        settings.ColorFormatId = format.Id;
        settings.AlphaPrecision = TextureAssetAlphaPrecision.A2;
        var item = new PlatformCookWorkItem("a2", Source, "texture", "x360", "runtime-texture", "a2.heasset", "textures/a2", "sourcehash", "settingshash", Xbox360TextureCookSettingsSerializer.Serialize(settings), []);
        var outputs = new Xbox360PlatformCookWorkItemExecutor().Execute([item], [], Package, CancellationToken.None);
        string stage = Path.Combine(Root, "disc");
        new Xbox360ContentStager().Stage([], outputs, Package, stage, CancellationToken.None);
        byte[] container = File.ReadAllBytes(Path.Combine(stage, outputs[0].RelativePath));
        using MemoryStream headerStream = new(container, false);
        Assert.Equal(EngineBinaryEndianness.BigEndian, EngineBinaryHeaderSerializer.Read(headerStream).Endianness);
        using TextureAsset cooked = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(container));
        Assert.Equal(TextureAssetAlphaPrecision.A2, cooked.AlphaPrecision);
        Assert.Equal(hardwareFormat, Xbox360NativeTextureCodec.ReadLayout(cooked).Format.HardwareFormat);
        byte[] preview = TextureAssetPixelCodec.DecodeToRgba32(cooked);
        Assert.Equal((byte)85, preview[3]);
        Assert.Equal((byte)170, preview[7]);
        Assert.Equal(original, source.Colors);
        Assert.Equal(TextureAssetAlphaPrecision.A8, source.AlphaPrecision);
    }

    /// <summary>Checks unknown cook kinds, platforms and settings cannot produce output.</summary>
    [Theory]
    [InlineData("material", "x360", "runtime-texture")]
    [InlineData("texture", "n64", "runtime-texture")]
    [InlineData("texture", "x360", "runtime-material")]
    public void UnsupportedWorkItemContractsFail(string kind, string platform, string artifact) {
        var item = new PlatformCookWorkItem("item", Source, kind, platform, artifact, "output.heasset", "output", "sourcehash", "settingshash", DefaultSettings(), []);
        Assert.Throws<InvalidOperationException>(() => new Xbox360PlatformCookWorkItemExecutor().Execute([item], [], Package, CancellationToken.None));
    }

    /// <summary>Checks cancelled batches preserve outputs even when there are no cook work items.</summary>
    [Fact]
    public void PrecancelledBatchPreservesPackage() {
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();
        Assert.Throws<OperationCanceledException>(() => new Xbox360PlatformCookWorkItemExecutor().Execute([], [], Package, cancellation.Token));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Removes only this test's verified contained fixture directory.</summary>
    public void Dispose() {
        string full = Path.GetFullPath(Root);
        if (!full.StartsWith(Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory)) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Fixture root escaped test output.");
        if (Directory.Exists(full)) Directory.Delete(full, true);
    }

    /// <summary>Creates one editor-shaped texture work item with native Xbox360 defaults.</summary>
    PlatformCookWorkItem Item(string path, string kind = "texture", string source = null) => new(path, source ?? Source, kind, "x360", "runtime-texture", path, path, "sourcehash", "settingshash", DefaultSettings(), []);

    /// <summary>Serializes the same defaults published by the platform capability.</summary>
    static string DefaultSettings() => Xbox360TextureCookSettingsSerializer.Serialize(Xbox360TextureCookSettingsSerializer.CreateDefaults());
}
