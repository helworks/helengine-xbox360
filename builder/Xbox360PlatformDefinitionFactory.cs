using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;

namespace helengine.x360.builder;

/// <summary>
/// Creates the editor-facing Xbox 360 platform contract.
/// </summary>
/// <remarks>
/// The codegen profile below must say exactly what tools/generate-core.ps1 says, because those
/// are the flags the native build actually compiles the generated core with. If the two drift,
/// the editor produces a core that does not match the headers it is compiled against, and the
/// failure appears as an unrelated compile error deep in generated code. Every value here is
/// therefore declared explicitly rather than left to a preset default.
/// </remarks>
public static class Xbox360PlatformDefinitionFactory {
    /// <summary>
    /// Engine features this target does not implement. The backend draws through raw Xenos
    /// microcode rather than the engine's shader system, and there is no debug overlay.
    /// Ids must exist in the engine feature catalog.
    /// </summary>
    const string ForcedDisabledFeatures = "debug_overlay";

    /// <summary>Builds the Xbox 360 platform definition.</summary>
    /// <returns>Platform definition consumed by the editor build graph.</returns>
    public static PlatformDefinition Create() {
        PlatformSettingDefinition[] buildSettings = [
            new("game-name", "Game Name", PlatformSettingKind.Text, "helengine", true, [])
        ];

        PlatformCodegenProfileDefinition codegen = new(
            "default",
            "Default",
            "Xbox 360 C# to C++ generated runtime profile (big-endian PowerPC, provider-backed containers)",
            PlatformCodegenLanguage.Cpp,
            // The console is big-endian. This describes the byte order of cooked data, not the
            // CPU, but here they agree and the cooked data is written to match the machine.
            PlatformSerializationEndianness.BigEndian,
            [
                new(PlatformCodegenSettingIds.ForcedDisabledFeatures, "Forced Disabled Features", PlatformSettingKind.Text, ForcedDisabledFeatures, true, []),
                new(PlatformCodegenSettingIds.EnabledFeatures, "Enabled Features", PlatformSettingKind.Text, "shaders", true, []),
                new("generated-math-convention", "Generated Math Convention", PlatformSettingKind.Text, "engine-row-vector", true, []),
                // 32-bit: the image is linked at a 32-bit base and every pointer in it is four
                // bytes, whatever the processor is capable of.
                new("pointer-size-bytes", "Pointer Size (Bytes)", PlatformSettingKind.Text, "4", true, []),
                new("include-project-defined-preprocessor-symbols", "Include Project Preprocessor Symbols", PlatformSettingKind.Boolean, "false", true, []),
                new("additional-preprocessor-symbols", "Additional Preprocessor Symbols", PlatformSettingKind.Text, "HELENGINE_CODEGEN_DISABLE_MENU_REFLECTION;HELENGINE_CODEGEN_DISABLE_RUNTIME_SCRIPT_REFLECTION;HELENGINE_CODEGEN_FATAL_ONLY", true, []),
                new("load-native-runtime-metadata", "Load Native Runtime Metadata", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-string", "Use std::string", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-vector", "Use std::vector", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-unordered-map", "Use std::unordered_map", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-unordered-set", "Use std::unordered_set", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-shared-ptr", "Use std::shared_ptr", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-function", "Use std::function", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-chrono", "Use std::chrono", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-math", "Use Standard Math", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-runtime-math-header", "Runtime Math Header", PlatformSettingKind.Text, "X360Math.hpp", true, []),
                new("codegen-use-hosted-file-system", "Use Hosted File System", PlatformSettingKind.Boolean, "false", true, []),
                // RTTI stays on because the generated generic dispatch requires it; codegen
                // refuses to convert the core without it.
                new("codegen-use-rtti", "Use Compiler RTTI", PlatformSettingKind.Boolean, "true", true, []),
                new("codegen-use-exceptions", "Use Exceptions", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-runtime-provider-header", "Runtime Provider Header", PlatformSettingKind.Text, "X360RuntimeProvider.hpp", true, []),
                // Full messages: a console image has no other way to say what the managed
                // runtime objected to, and a compacted message arrives empty.
                new("codegen-compact-native-exception-messages", "Compact Native Exception Messages", PlatformSettingKind.Boolean, "false", true, [])
            ]);

        PlatformMaterialSchemaDefinition materialSchema = new(
            Xbox360MaterialSchemaIds.OpaqueSchemaId,
            "Xbox 360 Opaque Unlit",
            [Xbox360MaterialSchemaIds.RendererFamilyId],
            [
                new(Xbox360MaterialSchemaIds.BaseColorFieldId, "Base Color", PlatformMaterialFieldKind.Color, "#FFFFFFFF", true, []),
                new(Xbox360MaterialSchemaIds.TextureRelativePathFieldId, "Texture", PlatformMaterialFieldKind.Text, string.Empty, false, []),
                new(Xbox360MaterialSchemaIds.LightingModeFieldId, "Lighting", PlatformMaterialFieldKind.Choice, Xbox360MaterialSchemaIds.UnlitLightingMode, true,
                    [Xbox360MaterialSchemaIds.UnlitLightingMode]),
                new(Xbox360MaterialSchemaIds.TransparencyFieldId, "Transparency", PlatformMaterialFieldKind.Choice, Xbox360MaterialSchemaIds.OpaqueTransparency, true,
                    [Xbox360MaterialSchemaIds.OpaqueTransparency])
            ]);

        PlatformBuildProfileDefinition[] profiles = [
            new("release", "Release", "Xbox 360 release image", Xbox360MaterialSchemaIds.RendererFamilyId, "default", buildSettings)
        ];

        return new PlatformDefinition(
            "x360",
            "Xbox 360",
            profiles,
            [new PlatformGraphicsProfileDefinition(Xbox360MaterialSchemaIds.RendererFamilyId, "Xbox 360 Unlit", "Xenos command-ring renderer, unlit vertex colours, depth tested", [])],
            [new PlatformAssetRequirementDefinition("scene", "Scene", true, ["helen"])],
            [materialSchema, new PlatformMaterialSchemaDefinition(
                Xbox360MaterialSchemaIds.MeshShaderSchemaId, "Xbox 360 Mesh Shader V1",
                [Xbox360MaterialSchemaIds.RendererFamilyId],
                [
                    new(Xbox360MaterialSchemaIds.ShaderAssetFieldId, "Shader Asset", PlatformMaterialFieldKind.Text, string.Empty, true, []),
                    new(Xbox360MaterialSchemaIds.VertexProgramFieldId, "Vertex Program", PlatformMaterialFieldKind.Text, string.Empty, true, []),
                    new(Xbox360MaterialSchemaIds.PixelProgramFieldId, "Pixel Program", PlatformMaterialFieldKind.Text, string.Empty, true, []),
                    new(Xbox360MaterialSchemaIds.BaseColorFieldId, "Base Color", PlatformMaterialFieldKind.Color, "#FFFFFFFF", true, [])
                ])],
            [
                new PlatformComponentSupportRule("helengine.meshcomponent", PlatformComponentSupportKind.Transform, "Static triangle-list meshes with 16-bit indices are drawn.", "Remove meshes that need 32-bit indices."),
                new PlatformComponentSupportRule("helengine.cameracomponent", PlatformComponentSupportKind.Transform, "Perspective cameras are supported and supply their own field of view.", "Keep at least one authored camera."),
                // Lights package and are then ignored. Declaring them unsupported instead fails
                // the build outright, and a scene that merely lights a cube it cannot light is
                // still a scene this target can draw; refusing it would make every authored
                // scene unbuildable until lighting exists. The description is what tells an
                // author the light will have no effect.
                new PlatformComponentSupportRule("helengine.lightcomponent", PlatformComponentSupportKind.Transform, "Packaged but ignored: the renderer is unlit.", "Expect no lighting until the renderer shades."),
                new PlatformComponentSupportRule("helengine.directionallightcomponent", PlatformComponentSupportKind.Transform, "Packaged but ignored: the renderer is unlit.", "Expect no lighting until the renderer shades.")
            ],
            [codegen],
            [new PlatformStorageProfileDefinition("embedded-archive", "Embedded Archive", PlatformStorageProfileKind.LooseFiles, "x360-embedded-archive", false)],
            [new PlatformMediaProfileDefinition("x360-xex", "XEX Image", PlatformMediaLayoutKind.InstallTree, false, true)],
            new RuntimeGenerationContract(RuntimeMaterialResolutionMode.CookedPlatformOwned, true, PackagedPathPolicy.ContentRelativeOnly),
            assetCookCapabilities: CreateAssetCookCapabilities());
    }

    /// <summary>Publishes native choices for authored textures and font atlases without lowering existing font resolution.</summary>
    static PlatformAssetCookCapabilityDefinition[] CreateAssetCookCapabilities() {
        string defaults = Xbox360TextureCookSettingsSerializer.Serialize(Xbox360TextureCookSettingsSerializer.CreateDefaults());
        PlatformTextureFormatCapabilityDefinition formats = CreateTextureFormatCapabilities();
        return [
            new("texture", "runtime-texture", PlatformAssetCookOwnershipKind.BuilderOwned, "x360-texture", defaults, formats),
            new("font-atlas-texture", "runtime-texture", PlatformAssetCookOwnershipKind.BuilderOwned, "x360-font-atlas-texture", defaults, formats)
        ];
    }

    /// <summary>Advertises cookable catalog formats and existing generic formats with their explicit alpha policies.</summary>
    static PlatformTextureFormatCapabilityDefinition CreateTextureFormatCapabilities() {
        List<string> formatIds = [];
        List<PlatformTextureFormatCombinationDefinition> combinations = [];
        HashSet<TextureAssetAlphaPrecision> alphaPrecisions = [];
        foreach (Xbox360NativeTextureFormatDefinition format in Xbox360NativeTextureFormatCatalog.Formats) {
            if (!format.SupportsCooking) continue;
            formatIds.Add(format.Id);
            foreach (TextureAssetAlphaPrecision alpha in format.SupportedAlphaPrecisions) {
                alphaPrecisions.Add(alpha);
                combinations.Add(new PlatformTextureFormatCombinationDefinition(format.Id, alpha));
            }
        }
        foreach (TextureAssetColorFormat format in Enum.GetValues<TextureAssetColorFormat>()) {
            if (!helengine.editor.TextureAssetProcessorSettings.TryResolveGenericColorFormat(format.ToString(), out _)) continue;
            formatIds.Add(format.ToString());
            foreach (TextureAssetAlphaPrecision alpha in Enum.GetValues<TextureAssetAlphaPrecision>()) {
                if (!TextureAssetPixelCodec.IsAlphaPrecisionSupported(format, alpha)) continue;
                alphaPrecisions.Add(alpha);
                combinations.Add(new PlatformTextureFormatCombinationDefinition(format.ToString(), alpha));
            }
        }
        return new PlatformTextureFormatCapabilityDefinition(formatIds, alphaPrecisions.OrderBy(alpha => alpha).ToArray(), combinations);
    }
}
