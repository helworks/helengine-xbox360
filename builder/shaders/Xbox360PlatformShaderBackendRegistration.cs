using helengine;

namespace helengine.x360.builder;

public sealed partial class Xbox360PlatformAssetBuilder : IShaderBackendRegistryContributor {
    /// <inheritdoc />
    public void RegisterShaderBackends(ShaderBackendRegistry shaderBackendRegistry) {
        if (shaderBackendRegistry == null) {
            throw new ArgumentNullException(nameof(shaderBackendRegistry));
        }

        shaderBackendRegistry.Register(new Xbox360ShaderBackend());
    }
}
