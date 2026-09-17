namespace helengine.x360.builder;

/// <summary>One manifest-declared cooked file after staging, addressed by the path the runtime opens.</summary>
/// <param name="RelativePath">Canonical forward-slash path relative to the content root.</param>
/// <param name="SourcePath">Absolute path of the staged copy the packer reads.</param>
public sealed record Xbox360StagedAsset(string RelativePath, string SourcePath);
