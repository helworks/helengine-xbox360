namespace helengine.x360.builder;

/// <summary>
/// Identifiers shared by the Xbox 360 material schema, its cooker and the renderer that
/// consumes the cooked bytes. They live in one place because a mismatch between the schema
/// the editor offers and the schema the cooker accepts is silent until an author picks the
/// field that was never wired up.
/// </summary>
public static class Xbox360MaterialSchemaIds {
    /// <summary>Graphics profile the schema belongs to.</summary>
    public const string RendererFamilyId = "x360-unlit";

    /// <summary>Schema for the only material this target currently cooks.</summary>
    public const string OpaqueSchemaId = "x360-opaque-unlit";

    /// <summary>Base colour field, the one value the renderer reads today.</summary>
    public const string BaseColorFieldId = "base-color";

    /// <summary>Texture path field, accepted by the schema but rejected by the cooker until textures exist.</summary>
    public const string TextureRelativePathFieldId = "texture-relative-path";

    /// <summary>Lighting mode field.</summary>
    public const string LightingModeFieldId = "lighting-mode";

    /// <summary>Only lighting mode implemented: the shader does no lighting.</summary>
    public const string UnlitLightingMode = "unlit";

    /// <summary>Transparency field.</summary>
    public const string TransparencyFieldId = "transparency";

    /// <summary>Only transparency implemented: the backend writes no blend state yet.</summary>
    public const string OpaqueTransparency = "opaque";
}
