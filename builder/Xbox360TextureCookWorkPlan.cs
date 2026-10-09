using helengine.baseplatform.Manifest;
using helengine.editor;

namespace helengine.x360.builder;

/// <summary>Stores a fully validated cook destination and its prepared serialized output until publication.</summary>
internal sealed class Xbox360TextureCookWorkPlan {
    /// <summary>Retains the manifest identity used when writing the cooked asset.</summary>
    public PlatformCookWorkItem WorkItem { get; init; }
    /// <summary>Retains validated native format and resize settings.</summary>
    public TextureAssetProcessorSettings Settings { get; init; }
    /// <summary>Identifies the absolute, contained package-source destination.</summary>
    public string DestinationPath { get; init; }
    /// <summary>Identifies the normalized runtime-relative staging path.</summary>
    public string RelativePath { get; init; }
    /// <summary>Holds serialized bytes only after decoding and cooking have both succeeded.</summary>
    public byte[] Payload { get; set; }
}
