using helengine.files;

namespace helengine.x360.builder;

/// <summary>Writes a big-endian HELE container while preserving opaque GPU texels in their native little-endian layout.</summary>
public static class Xbox360CookedTextureSerializer {
    /// <summary>Uses the shared texture payload writer and versioned header rather than duplicating asset serialization.</summary>
    public static byte[] SerializeToBytes(TextureAsset texture) {
        ArgumentNullException.ThrowIfNull(texture);
        using MemoryStream stream = new();
        TextureAssetPayloadSerializer serializer = new();
        serializer.Validate(texture);
        EngineBinaryHeaderSerializer.Write(stream, new EngineBinaryHeader(EngineBinaryEndianness.BigEndian,
            EditorAssetBinarySerializer.CurrentVersion, EditorAssetBinarySerializer.FormatId,
            (ushort)EditorAssetBinarySerializer.RecordKind, (ushort)serializer.ValueKind));
        using (helengine.files.EngineBinaryWriter writer = helengine.files.EngineBinaryWriter.Create(stream, EngineBinaryEndianness.BigEndian, true)) {
            serializer.Write(writer, texture);
        }
        return stream.ToArray();
    }
}
