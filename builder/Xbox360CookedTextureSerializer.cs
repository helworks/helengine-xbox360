using helengine.files;

namespace helengine.x360.builder;

/// <summary>Writes a big-endian HELE container while preserving opaque GPU texels in their native little-endian layout.</summary>
public static class Xbox360CookedTextureSerializer {
    /// <summary>Uses the shared texture payload writer and versioned header rather than duplicating asset serialization.</summary>
    public static byte[] SerializeToBytes(TextureAsset texture) {
        return SerializeToBytes(texture, RealRepresentation.Policy);
    }

    /// <summary>Writes the versioned target policy before the native texture payload.</summary>
    public static byte[] SerializeToBytes(TextureAsset texture, NumericPrecisionPolicy numericPolicy) {
        ArgumentNullException.ThrowIfNull(texture);
        RealBinaryCodec.ValidatePolicy(numericPolicy);
        using MemoryStream stream = new();
        TextureAssetPayloadSerializer serializer = new();
        serializer.Validate(texture);
        EngineBinaryHeaderSerializer.Write(stream, new EngineBinaryHeader(EngineBinaryEndianness.BigEndian,
            EditorAssetBinarySerializer.CurrentVersion, EditorAssetBinarySerializer.FormatId,
            (ushort)EditorAssetBinarySerializer.RecordKind, (ushort)serializer.ValueKind));
        using (helengine.files.EngineBinaryWriter writer = helengine.files.EngineBinaryWriter.Create(stream, EngineBinaryEndianness.BigEndian, true)) {
            writer.NumericPolicy = numericPolicy;
            writer.WriteNumericPolicy();
            serializer.Write(writer, texture);
        }
        return stream.ToArray();
    }
}
