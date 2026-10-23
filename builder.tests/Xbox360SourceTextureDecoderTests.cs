using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

/// <summary>Checks authored pixel decoding independently from the Xenos texture payload.</summary>
public sealed class Xbox360SourceTextureDecoderTests {
    /// <summary>Retains the RGBA source contract on Windows and portable build hosts.</summary>
    [Fact]
    public void AuthoredPngDecodesToOwnedRgbaPixels() {
        string root = Path.Combine(AppContext.BaseDirectory, "source-decoder");
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "pixel.png");
        byte[] png = Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
        try {
            File.WriteAllBytes(path, png);
            using TextureAsset decoded = new Xbox360SourceTextureDecoder().Decode(path);
            Assert.Equal(1, decoded.Width);
            Assert.Equal(1, decoded.Height);
            Assert.Equal(TextureAssetColorFormat.Rgba32, decoded.ColorFormat);
            Assert.Equal(TextureAssetAlphaPrecision.A8, decoded.AlphaPrecision);
            Assert.NotNull(decoded.PaletteColors);
            Assert.Empty(decoded.PaletteColors);
            Assert.Equal(new byte[] {255, 0, 0, 255}, decoded.Colors);
            Assert.Equal(png, File.ReadAllBytes(path));
        } finally {
            File.Delete(path);
            Directory.Delete(root);
        }
    }
}
