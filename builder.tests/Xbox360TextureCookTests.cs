using helengine;
using helengine.editor;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

/// <summary>Exercises catalog capabilities, strict settings, resizing, palette preparation and native serialization.</summary>
public sealed class Xbox360TextureCookTests {
    /// <summary>Supplies every advertised native hardware format, including lossy, palette and DXT formats.</summary>
    public static IEnumerable<object[]> NativeFormats => Xbox360NativeTextureFormatCatalog.Formats.Where(format => format.SupportsCooking).Select(format => new object[] { format.Id });

    /// <summary>Ensures both texture kinds publish precisely the catalog combinations and preserve atlas resolution by default.</summary>
    [Fact]
    public void CapabilitiesExposeAllFormatsAndFullResolutionAtlasDefaults() {
        var definition = Xbox360PlatformDefinitionFactory.Create();
        Assert.Equal(64, Xbox360NativeTextureFormatCatalog.Formats.Length);
        Assert.Equal(44, Xbox360NativeTextureFormatCatalog.Formats.Count(format => format.SupportsCooking));
        Assert.Equal(2, definition.AssetCookCapabilities.Length);
        foreach (var capability in definition.AssetCookCapabilities) {
            Assert.Equal("BuilderOwned", capability.OwnershipKind.ToString());
            Assert.Equal("runtime-texture", capability.TargetArtifactKind);
            var defaults = Xbox360TextureCookSettingsSerializer.Deserialize(capability.DefaultSerializedPlatformSettings);
            Assert.Equal(8192, defaults.MaxResolution);
            Assert.Equal("Xbox360.Linear.8_8_8_8", defaults.ColorFormatId);
            Assert.Equal(TextureAssetAlphaPrecision.A8, defaults.AlphaPrecision);
            Assert.Equal(Xbox360NativeTextureFormatCatalog.Formats.Where(format => format.SupportsCooking).Select(format => format.Id), capability.TextureFormatCapabilities.SupportedColorFormatIds.Take(44));
            Assert.Equal(55, capability.TextureFormatCapabilities.SupportedColorFormatIds.Length);
            foreach (var format in Xbox360NativeTextureFormatCatalog.Formats) {
                if (!format.SupportsCooking) {
                    Assert.DoesNotContain(format.Id, capability.TextureFormatCapabilities.SupportedColorFormatIds);
                    continue;
                }
                Assert.Equal(format.SupportedAlphaPrecisions,
                    capability.TextureFormatCapabilities.SupportedCombinations.Where(item => item.ColorFormatId == format.Id).Select(item => item.AlphaPrecision));
            }
        }
        Assert.Contains(definition.AssetCookCapabilities, item => item.SourceAssetKind == "font-atlas-texture");
    }

    /// <summary>Verifies each advertised format cooks and survives HELE serialization with matching native layout and identity.</summary>
    [Theory]
    [MemberData(nameof(NativeFormats))]
    public void EveryNativeFormatCooksAndSerializesWithoutMutatingSource(string formatId) {
        using TextureAsset source = Source(5, 3);
        byte[] sourceBytes = (byte[])source.Colors.Clone();
        Xbox360NativeTextureFormatCatalog.TryGetFormat(formatId, out var format);
        var settings = Xbox360TextureCookSettingsSerializer.CreateDefaults();
        settings.ColorFormatId = formatId;
        settings.AlphaPrecision = format.DefaultAlphaPrecision;
        using TextureAsset cooked = new Xbox360TextureCooker().Cook(source, settings);
        Assert.Equal(TextureAssetColorFormat.Xbox360Native, cooked.ColorFormat);
        Assert.Equal(source.Id, cooked.Id);
        Assert.Equal(source.RuntimeAssetId, cooked.RuntimeAssetId);
        Assert.Equal(source.AuthoringAssetId, cooked.AuthoringAssetId);
        Assert.Equal(source.FormerAuthoringAssetIds, cooked.FormerAuthoringAssetIds);
        Assert.NotSame(source.FormerAuthoringAssetIds, cooked.FormerAuthoringAssetIds);
        Assert.Equal(sourceBytes, source.Colors);
        using TextureAsset read = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(helengine.files.AssetSerializer.SerializeToBytes(cooked)));
        Assert.Equal(cooked.Colors, read.Colors);
        Assert.Equal(cooked.PaletteColors, read.PaletteColors);
        var layout = Xbox360NativeTextureCodec.ReadLayout(read);
        Assert.Equal(format.HardwareFormat, layout.Format.HardwareFormat);
        Assert.Equal(5 * 3 * 4, TextureAssetPixelCodec.DecodeToRgba32(read).Length);
    }

    /// <summary>Checks resize happens in logical RGBA space before pitched native storage is allocated.</summary>
    [Fact]
    public void ResizePreservesAspectAndSourceBeforeNativeEncoding() {
        using TextureAsset source = Source(8, 4);
        var settings = Xbox360TextureCookSettingsSerializer.CreateDefaults();
        settings.MaxResolution = 4;
        using TextureAsset cooked = new Xbox360TextureCooker().Cook(source, settings);
        Assert.Equal((ushort)4, cooked.Width);
        Assert.Equal((ushort)2, cooked.Height);
        Assert.Equal((ushort)8, source.Width);
        byte[] decoded = Xbox360NativeTextureCodec.Decode(cooked);
        Assert.Equal(source.Colors.AsSpan(0, 4).ToArray(), decoded.AsSpan(0, 4).ToArray());
        Assert.Equal(256, Xbox360NativeTextureCodec.ReadLayout(cooked).PitchBytes);
    }

    /// <summary>Checks generated atlas dimensions below the default limit are retained exactly.</summary>
    [Fact]
    public void FontAtlasDefaultKeepsAuthoredDimensions() {
        using TextureAsset source = Source(512, 256);
        using TextureAsset cooked = new Xbox360TextureCooker().Cook(source, Xbox360TextureCookSettingsSerializer.CreateDefaults());
        Assert.Equal(source.Width, cooked.Width);
        Assert.Equal(source.Height, cooked.Height);
    }

    /// <summary>Rejects malformed and unsupported settings rather than falling back to the default format.</summary>
    [Theory]
    [InlineData("{}")]
    [InlineData("[]")]
    [InlineData("{\"maxResolution\":8192,\"colorFormat\":\"GxRgb5A3\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":-1,\"colorFormat\":\"Xbox360.Linear.8_8_8_8\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":8193,\"colorFormat\":\"Xbox360.Linear.8_8_8_8\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":8192,\"colorFormat\":\"Xbox360.Linear.8_8_8_8\",\"alphaPrecision\":\"255\"}")]
    [InlineData("{\"maxResolution\":8192,\"colorFormat\":\"Rgba32\",\"alphaPrecision\":\"A2\"}")]
    [InlineData("{\"maxResolution\":8192,\"colorFormat\":\"Xbox360.Linear.8_8_8_8\",\"alphaPrecision\":\"A8\",\"extra\":true}")]
    [InlineData("{\"maxResolution\":8192,\"maxResolution\":32,\"colorFormat\":\"Xbox360.Linear.8_8_8_8\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":8192,\"colorFormat\":\"Xbox360.Linear.8_8_8_8\",\"alphaPrecision\":\"A8\",\"indexingMethod\":\"QuantizedIndexed\"}")]
    [InlineData("{\"maxResolution\":8192,\"colorFormat\":\"Indexed8\",\"alphaPrecision\":\"A8\",\"indexingMethod\":\"Unknown\"}")]
    public void InvalidSettingsFail(string json) => Assert.Throws<InvalidOperationException>(() => Xbox360TextureCookSettingsSerializer.Deserialize(json));

    /// <summary>Rejects every documented non-cookable code instead of advertising it or substituting RGBA.</summary>
    [Fact]
    public void UnsupportedCatalogCodesFailExplicitly() {
        foreach (var format in Xbox360NativeTextureFormatCatalog.Formats.Where(format => !format.SupportsCooking)) {
            var settings = Xbox360TextureCookSettingsSerializer.CreateDefaults();
            settings.ColorFormatId = format.Id;
            Assert.False(string.IsNullOrEmpty(format.UnsupportedReason));
            var failure = Assert.Throws<InvalidOperationException>(() => Xbox360TextureCookSettingsSerializer.Validate(settings));
            Assert.Contains(format.UnsupportedReason, failure.Message);
        }
    }

    /// <summary>Preserves authored generic settings and uncapped resizing instead of silently converting them to a native default.</summary>
    [Theory]
    [InlineData("Rgba32")]
    [InlineData("Indexed4")]
    [InlineData("Indexed8")]
    public void AuthoredGenericFormatsRemainSupported(string formatId) {
        using TextureAsset source = Source(4, 2);
        byte[] bytes = (byte[])source.Colors.Clone();
        var settings = Xbox360TextureCookSettingsSerializer.CreateDefaults();
        settings.MaxResolution = 0;
        settings.ColorFormatId = formatId;
        if (formatId != "Rgba32") settings.IndexingMethodId = "QuantizedIndexed";
        var parsed = Xbox360TextureCookSettingsSerializer.Deserialize(Xbox360TextureCookSettingsSerializer.Serialize(settings));
        using TextureAsset cooked = new Xbox360TextureCooker().Cook(source, parsed);
        Assert.Equal(formatId, cooked.ColorFormat.ToString());
        Assert.Equal(bytes, TextureAssetPixelCodec.DecodeToRgba32(cooked));
        Assert.Equal(bytes, source.Colors);
        Assert.NotSame(source, cooked);
    }

    /// <summary>Checks native bytes remain unchanged inside a big-endian container and source decoding returns an independent RGBA asset.</summary>
    [Fact]
    public void BigEndianCookedContainerPreservesNativeTexels() {
        using TextureAsset source = Source(3, 2);
        using TextureAsset cooked = new Xbox360TextureCooker().Cook(source, Xbox360TextureCookSettingsSerializer.CreateDefaults());
        byte[] payload = Xbox360CookedTextureSerializer.SerializeToBytes(cooked);
        using MemoryStream headerStream = new(payload, false);
        Assert.Equal(EngineBinaryEndianness.BigEndian, EngineBinaryHeaderSerializer.Read(headerStream).Endianness);
        using MemoryStream stream = new(payload, false);
        using TextureAsset packaged = Assert.IsType<TextureAsset>(PackagedAssetBinarySerializer.Deserialize(stream));
        Assert.Equal(cooked.Colors, packaged.Colors);
        Assert.Equal(source.Colors, Xbox360NativeTextureCodec.Decode(packaged));
    }

    /// <summary>Constructs varied RGBA pixels with stable authored and runtime identity for lifecycle assertions.</summary>
    internal static TextureAsset Source(ushort width, ushort height) {
        byte[] colors = new byte[width * height * 4];
        for (int offset = 0; offset < colors.Length; offset += 4) {
            colors[offset] = (byte)(offset * 17);
            colors[offset + 1] = (byte)(255 - offset);
            colors[offset + 2] = (byte)(offset * 7);
            colors[offset + 3] = 255;
        }
        return new TextureAsset { Id = "textures/source", RuntimeAssetId = 123, AuthoringAssetId = "authored-id", FormerAuthoringAssetIds = ["former-id"], Width = width, Height = height, Colors = colors, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8 };
    }
}
