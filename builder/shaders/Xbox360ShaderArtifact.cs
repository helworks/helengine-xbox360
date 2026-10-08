namespace helengine.x360.builder;

/// <summary>An explicit asset identity associated with one validated Xbox 360 VS/PS pair.</summary>
public sealed class Xbox360ShaderArtifact {
    public Xbox360ShaderArtifact(string assetId, Xbox360ShaderProgramPair programs) {
        AssetId = assetId;
        Programs = programs ?? throw new ArgumentNullException(nameof(programs));
    }

    public string AssetId { get; }
    public Xbox360ShaderProgramPair Programs { get; }
}
