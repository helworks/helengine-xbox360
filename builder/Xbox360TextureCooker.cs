using helengine.editor;

namespace helengine.x360.builder;

/// <summary>Resizes a private RGBA copy before encoding the requested Xenos or compatible generic texture payload.</summary>
public sealed class Xbox360TextureCooker {
    /// <summary>Shares the editor's aspect-preserving resize implementation with platform cooks.</summary>
    readonly TextureAssetProcessor Processor = new();

    /// <summary>Produces a new native texture while retaining authored identity and leaving source bytes intact.</summary>
    public TextureAsset Cook(TextureAsset source, TextureAssetProcessorSettings settings) {
        ArgumentNullException.ThrowIfNull(source);
        Xbox360TextureCookSettingsSerializer.Validate(settings);
        TextureAsset rgba = new() {
            Width = source.Width, Height = source.Height,
            ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8,
            Colors = TextureAssetPixelCodec.DecodeToRgba32(source), PaletteColors = Array.Empty<byte>()
        };
        CopyIdentity(source, rgba);
        TextureAsset resized = null;
        TextureAsset result = null;
        try {
            resized = Processor.Apply(rgba, new TextureAssetProcessorSettings {
                MaxResolution = settings.MaxResolution, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8
            });
            if (resized.Width > 8192 || resized.Height > 8192) throw new InvalidOperationException("Xbox360 cooked texture dimensions cannot exceed 8192 pixels.");
            if (TextureAssetProcessorSettings.TryResolveGenericColorFormat(settings.ColorFormatId, out TextureAssetColorFormat genericFormat)) {
                result = Processor.Apply(resized, new TextureAssetProcessorSettings {
                    MaxResolution = 0, ColorFormat = genericFormat, AlphaPrecision = settings.AlphaPrecision, IndexingMethodId = settings.IndexingMethodId
                });
                CopyIdentity(source, result);
                return result;
            }
            result = Xbox360NativeTextureCodec.Encode(resized, settings.ColorFormatId, settings.AlphaPrecision);
            CopyIdentity(source, result);
            return result;
        } finally {
            if (resized != null && !ReferenceEquals(resized, rgba) && !ReferenceEquals(resized, result)) resized.Dispose();
            if (!ReferenceEquals(rgba, result)) rgba.Dispose();
        }
    }

    /// <summary>Copies identity metadata while keeping the mutable alias-array container independent.</summary>
    internal static void CopyIdentity(TextureAsset source, TextureAsset destination) {
        destination.Id = source.Id;
        destination.RuntimeAssetId = source.RuntimeAssetId;
        destination.AuthoringAssetId = source.AuthoringAssetId;
        destination.FormerAuthoringAssetIds = source.FormerAuthoringAssetIds == null ? Array.Empty<string>() : (string[])source.FormerAuthoringAssetIds.Clone();
        destination.IsEngineOwned = source.IsEngineOwned;
    }
}
