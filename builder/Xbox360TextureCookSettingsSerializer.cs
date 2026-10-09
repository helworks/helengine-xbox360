using System.Text.Json;
using helengine.editor;

namespace helengine.x360.builder;

/// <summary>Serializes and validates native Xbox360 texture settings carried by editor cook work items.</summary>
public static class Xbox360TextureCookSettingsSerializer {
    /// <summary>Creates defaults that retain full-resolution font atlases and eight-bit alpha.</summary>
    public static TextureAssetProcessorSettings CreateDefaults() => new() {
        MaxResolution = 8192,
        ColorFormatId = "Xbox360.Linear.8_8_8_8",
        AlphaPrecision = TextureAssetAlphaPrecision.A8,
        IndexingMethodId = string.Empty
    };

    /// <summary>Writes the platform-owned format id without routing it through the generic format enum.</summary>
    public static string Serialize(TextureAssetProcessorSettings settings) {
        Validate(settings);
        return JsonSerializer.Serialize(new Dictionary<string, object> {
            ["maxResolution"] = settings.MaxResolution,
            ["colorFormat"] = settings.ColorFormatId,
            ["alphaPrecision"] = settings.AlphaPrecision.ToString(),
            ["indexingMethod"] = settings.IndexingMethodId ?? string.Empty
        });
    }

    /// <summary>Rejects missing, duplicate, unknown, or unsupported settings before cooking any pixels.</summary>
    public static TextureAssetProcessorSettings Deserialize(string serializedSettings) {
        if (string.IsNullOrWhiteSpace(serializedSettings)) throw new ArgumentException("Xbox360 texture settings are required.", nameof(serializedSettings));
        using JsonDocument document = JsonDocument.Parse(serializedSettings);
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object) throw new InvalidOperationException("Xbox360 texture settings must be an object.");
        HashSet<string> keys = new(StringComparer.Ordinal);
        foreach (JsonProperty property in root.EnumerateObject()) {
            if (!keys.Add(property.Name)) throw new InvalidOperationException($"Duplicate Xbox360 texture setting '{property.Name}'.");
            if (property.Name != "maxResolution" && property.Name != "colorFormat" && property.Name != "alphaPrecision" && property.Name != "indexingMethod") {
                throw new InvalidOperationException($"Unknown Xbox360 texture setting '{property.Name}'.");
            }
        }
        if (!root.TryGetProperty("maxResolution", out JsonElement maximum) || !root.TryGetProperty("colorFormat", out JsonElement format) || !root.TryGetProperty("alphaPrecision", out JsonElement alpha)) {
            throw new InvalidOperationException("Xbox360 texture settings require maxResolution, colorFormat, and alphaPrecision.");
        }
        string alphaName = alpha.GetString();
        if (!Enum.TryParse(alphaName, false, out TextureAssetAlphaPrecision precision) || precision.ToString() != alphaName) {
            throw new InvalidOperationException($"Unsupported Xbox360 alpha precision '{alphaName}'.");
        }
        TextureAssetProcessorSettings settings = new() {
            MaxResolution = maximum.GetInt32(),
            ColorFormatId = format.GetString(),
            AlphaPrecision = precision,
            IndexingMethodId = root.TryGetProperty("indexingMethod", out JsonElement indexing) ? indexing.GetString() : string.Empty
        };
        Validate(settings);
        return settings;
    }

    /// <summary>Checks catalog format/alpha compatibility and restricts palette indexing to the implemented quantizer.</summary>
    public static void Validate(TextureAssetProcessorSettings settings) {
        ArgumentNullException.ThrowIfNull(settings);
        if (settings.MaxResolution < 0 || settings.MaxResolution > 8192) throw new InvalidOperationException("Xbox360 maxResolution must be zero (uncapped) or between 1 and 8192.");
        bool isPaletted;
        if (TextureAssetProcessorSettings.TryResolveGenericColorFormat(settings.ColorFormatId, out TextureAssetColorFormat genericFormat)) {
            if (!TextureAssetPixelCodec.IsAlphaPrecisionSupported(genericFormat, settings.AlphaPrecision)) throw new InvalidOperationException($"Xbox360 generic format '{genericFormat}' does not support '{settings.AlphaPrecision}' alpha.");
            isPaletted = genericFormat == TextureAssetColorFormat.Indexed4 || genericFormat == TextureAssetColorFormat.Indexed8;
        } else if (Xbox360NativeTextureFormatCatalog.TryGetFormat(settings.ColorFormatId, out Xbox360NativeTextureFormatDefinition format)) {
            if (!format.SupportsCooking) throw new InvalidOperationException($"Xbox360 format '{format.Id}' cannot be cooked: {format.UnsupportedReason}");
            if (!format.SupportsAlpha(settings.AlphaPrecision)) throw new InvalidOperationException($"Xbox360 format '{format.Id}' does not support '{settings.AlphaPrecision}' alpha.");
            isPaletted = false;
        } else {
            throw new InvalidOperationException($"Unsupported Xbox360 texture format '{settings.ColorFormatId}'.");
        }
        string indexing = settings.IndexingMethodId ?? string.Empty;
        if (isPaletted) {
            if (indexing.Length != 0 && indexing != TextureAssetIndexingMethod.QuantizedIndexed.ToString()) throw new InvalidOperationException($"Unsupported Xbox360 indexing method '{indexing}'.");
        } else if (indexing.Length != 0) {
            throw new InvalidOperationException("Xbox360 indexingMethod is only supported for a palette format.");
        }
    }
}
