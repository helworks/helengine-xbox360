namespace helengine.x360.builder;

/// <summary>
/// Runs the native toolchain that compiles the generated core and links the image.
/// </summary>
/// <remarks>
/// This exists as an interface so a test can drive the builder without a cross compiler or a
/// Docker daemon present. The base platform contract does not define one; every port invents
/// its own for the same reason.
/// </remarks>
public interface IXbox360NativeBuildExecutor {
    /// <summary>Builds the image and throws if it was not produced.</summary>
    /// <param name="generatedCoreCppRootPath">Generated core the native build compiles.</param>
    /// <param name="contentRootPath">Staged cooked content packed into the image.</param>
    /// <param name="catalogRootPath">Directory holding the generated scene catalog.</param>
    /// <param name="outputRootPath">Directory receiving the image.</param>
    /// <param name="cancellationToken">Cancellation token honoured by killing the process tree.</param>
    void Build(
        string generatedCoreCppRootPath,
        string contentRootPath,
        string catalogRootPath,
        string outputRootPath,
        CancellationToken cancellationToken);
}
