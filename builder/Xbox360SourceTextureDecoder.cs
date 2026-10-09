using System.Drawing;

namespace helengine.x360.builder;

/// <summary>Reads authored images or serialized textures into independently owned RGBA pixels for cooking.</summary>
public sealed class Xbox360SourceTextureDecoder {
    /// <summary>Detects generated engine atlases by payload rather than file extension.</summary>
    public TextureAsset Decode(string sourceAssetPath) {
        if (string.IsNullOrWhiteSpace(sourceAssetPath)) throw new ArgumentException("Source texture path is required.", nameof(sourceAssetPath));
        byte[] bytes = File.ReadAllBytes(sourceAssetPath);
        if (bytes.Length >= 4 && bytes[0] == 'H' && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'E') {
            Asset asset = helengine.files.AssetSerializer.DeserializeFromBytes(bytes);
            if (asset is not TextureAsset texture) throw new InvalidOperationException("Xbox360 cook source is not a texture asset.");
            try {
                TextureAsset decoded = new() {
                    Width = texture.Width, Height = texture.Height,
                    ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8,
                    Colors = TextureAssetPixelCodec.DecodeToRgba32(texture), PaletteColors = Array.Empty<byte>()
                };
                Xbox360TextureCooker.CopyIdentity(texture, decoded);
                return decoded;
            } finally { texture.Dispose(); }
        }
        if (!OperatingSystem.IsWindowsVersionAtLeast(6, 1)) throw new PlatformNotSupportedException("Xbox360 authored image decoding requires the Windows image decoder; serialized engine textures are portable.");
        using MemoryStream stream = new(bytes, false);
        using Bitmap image = new(stream);
        ushort width = checked((ushort)image.Width);
        ushort height = checked((ushort)image.Height);
        byte[] colors = new byte[checked(image.Width * image.Height * 4)];
        int offset = 0;
        for (int y = 0; y < image.Height; y++) {
            for (int x = 0; x < image.Width; x++) {
                Color pixel = image.GetPixel(x, y);
                colors[offset++] = pixel.R;
                colors[offset++] = pixel.G;
                colors[offset++] = pixel.B;
                colors[offset++] = pixel.A;
            }
        }
        return new TextureAsset { Width = width, Height = height, Colors = colors, PaletteColors = Array.Empty<byte>(), ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8 };
    }
}
