using helengine.baseplatform.Builders;
using helengine.baseplatform.Reporting;

namespace helengine.x360.builder.tests;

/// <summary>Accepts progress and diagnostics while integration tests inspect the returned build report.</summary>
internal sealed class TextureBuildReporter : IPlatformBuildProgressReporter, IPlatformBuildDiagnosticReporter {
    /// <summary>Accepts a progress update without triggering console or native work.</summary>
    public void Report(PlatformBuildProgressUpdate update) { }
    /// <summary>Accepts a diagnostic already retained by the builder's report.</summary>
    public void Report(PlatformBuildDiagnostic diagnostic) { }
}
