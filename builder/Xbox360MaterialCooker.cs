using System.Globalization;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.x360.builder;

/// <summary>
/// Cooks the opaque unlit material subset into the engine's platform material payload.
/// </summary>
/// <remarks>
/// Everything this target cannot draw yet is rejected with a message naming the material,
/// rather than approximated. A texture silently dropped or a translucent material silently
/// made opaque produces a scene that looks almost right, which is harder to diagnose than a
/// build that stops and says which material it could not cook.
/// </remarks>
public sealed class Xbox360MaterialCooker {
    /// <summary>Validates the requested material against the schema and serializes the cooked payload.</summary>
    /// <param name="request">Material identity, schema id and resolved field values supplied by the editor.</param>
    /// <returns>The serialized platform material bytes with no shader dependencies.</returns>
    public PlatformMaterialCookResult Cook(PlatformMaterialCookRequest request) {
        if (request == null) throw new ArgumentNullException(nameof(request));
        if (!string.Equals(request.TargetPlatformId, "x360", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"The Xbox 360 builder cannot cook materials for target platform '{request.TargetPlatformId}'.");
        }
        bool meshShader = string.Equals(request.SchemaId, Xbox360MaterialSchemaIds.MeshShaderSchemaId, StringComparison.OrdinalIgnoreCase);
        if (!meshShader && !string.Equals(request.SchemaId, Xbox360MaterialSchemaIds.OpaqueSchemaId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"The Xbox 360 builder does not support material schema '{request.SchemaId}'.");
        }

        string texture = Resolve(request.FieldValues, Xbox360MaterialSchemaIds.TextureRelativePathFieldId);
        if (!string.IsNullOrWhiteSpace(texture)) {
            throw new InvalidOperationException($"The Xbox 360 renderer has no texture path yet, so '{request.MaterialRelativePath}' cannot be cooked with a texture binding.");
        }

        string lighting = Resolve(request.FieldValues, Xbox360MaterialSchemaIds.LightingModeFieldId, Xbox360MaterialSchemaIds.UnlitLightingMode);
        if (!string.Equals(lighting, Xbox360MaterialSchemaIds.UnlitLightingMode, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"The Xbox 360 renderer is unlit; '{request.MaterialRelativePath}' requested '{lighting}'.");
        }

        string transparency = Resolve(request.FieldValues, Xbox360MaterialSchemaIds.TransparencyFieldId, Xbox360MaterialSchemaIds.OpaqueTransparency);
        if (!string.Equals(transparency, Xbox360MaterialSchemaIds.OpaqueTransparency, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"The Xbox 360 backend writes no blend state yet, so '{request.MaterialRelativePath}' must be opaque; it requested '{transparency}'.");
        }

        ParseColor(Resolve(request.FieldValues, Xbox360MaterialSchemaIds.BaseColorFieldId, "#FFFFFFFF"), out byte r, out byte g, out byte b, out byte a);
        if (a != 255) {
            throw new InvalidOperationException($"The Xbox 360 opaque schema requires an alpha of FF; '{request.MaterialRelativePath}' has alpha {a:X2}.");
        }

        if (meshShader) {
            var dependency = new PlatformShaderDependency(
                Resolve(request.FieldValues, Xbox360MaterialSchemaIds.ShaderAssetFieldId),
                Resolve(request.FieldValues, Xbox360MaterialSchemaIds.VertexProgramFieldId),
                Resolve(request.FieldValues, Xbox360MaterialSchemaIds.PixelProgramFieldId),
                Xbox360MaterialSchemaIds.MeshVariant);
            _ = Xbox360ShaderArtifactCooker.RelativePath(dependency);
            byte[] color = new byte[16];
            float[] channels = [r / 255f, g / 255f, b / 255f, 1f];
            for (int i = 0; i < 4; i++)
                System.Buffers.Binary.BinaryPrimitives.WriteSingleLittleEndian(color.AsSpan(i * 4, 4), channels[i]);
            using var material = new helengine.ShaderMaterialAsset {
                Id = request.MaterialAssetId,
                ShaderAssetId = dependency.ShaderAssetId,
                VertexProgram = dependency.VertexProgramName,
                PixelProgram = dependency.PixelProgramName,
                Variant = dependency.VariantName,
                CastsShadows = false,
                ReceivesShadows = false,
                ConstantBuffers = [helengine.MaterialConstantBufferAsset.Create("Material", color)]
            };
            return new PlatformMaterialCookResult(helengine.ShaderMaterialAssetBinarySerializer.SerializeToBytes(material), [dependency]);
        }

        helengine.PlatformMaterialAsset cooked = new() {
            Id = request.MaterialAssetId,
            RendererFamilyId = Xbox360MaterialSchemaIds.RendererFamilyId,
            TextureRelativePath = string.Empty,
            DoubleSided = false,
            UseVertexColor = false,
            Lit = false,
            BaseColorR = r,
            BaseColorG = g,
            BaseColorB = b,
            BaseColorA = a
        };
        return new PlatformMaterialCookResult(helengine.files.AssetSerializer.SerializeToBytes(cooked, request.NumericPolicy), []);
    }

    /// <summary>Returns the trimmed field value, or the fallback when the field is absent or blank.</summary>
    static string Resolve(IReadOnlyDictionary<string, string> values, string key, string fallback = "") {
        if (values == null) throw new ArgumentNullException(nameof(values));
        return values.TryGetValue(key, out string value) && !string.IsNullOrWhiteSpace(value) ? value.Trim() : fallback;
    }

    /// <summary>Parses #RRGGBB or #RRGGBBAA into channels, defaulting alpha to opaque.</summary>
    static void ParseColor(string value, out byte r, out byte g, out byte b, out byte a) {
        if (string.IsNullOrWhiteSpace(value)) {
            throw new InvalidOperationException("An Xbox 360 material needs a base color.");
        }
        string normalized = value.Trim();
        if (normalized.StartsWith('#')) {
            normalized = normalized[1..];
        }
        if (normalized.Length == 6) {
            normalized += "FF";
        }
        if (normalized.Length != 8) {
            throw new InvalidOperationException($"Color '{value}' must use #RRGGBB or #RRGGBBAA format.");
        }
        try {
            r = byte.Parse(normalized.AsSpan(0, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            g = byte.Parse(normalized.AsSpan(2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            b = byte.Parse(normalized.AsSpan(4, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            a = byte.Parse(normalized.AsSpan(6, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        } catch (FormatException exception) {
            throw new InvalidOperationException($"Color '{value}' contains invalid hexadecimal channels.", exception);
        }
    }
}
