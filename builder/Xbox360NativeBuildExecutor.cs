using System.Diagnostics;

namespace helengine.x360.builder;

/// <summary>
/// Invokes tools/build-image.ps1 in the Xbox 360 repository, then checks the image exists.
/// </summary>
/// <remarks>
/// The exit code is not trusted on its own. A build script can exit zero having done nothing,
/// or having left a previous run's output in place, so the caller deletes the expected image
/// first and this verifies it came back. Both output streams are captured and attached to the
/// failure, because a native build that fails inside Docker says nothing useful otherwise.
/// </remarks>
public sealed class Xbox360NativeBuildExecutor : IXbox360NativeBuildExecutor {
    /// <summary>Environment variable naming the Xbox 360 repository, when it is not beside the builder.</summary>
    public const string RepositoryRootVariable = "HELENGINE_X360_REPOSITORY_ROOT";

    /// <inheritdoc />
    public void Build(
        string generatedCoreCppRootPath,
        string contentRootPath,
        string catalogRootPath,
        string outputRootPath,
        string nativeObjectCacheRootPath,
        CancellationToken cancellationToken) {
        string repositoryRoot = ResolveRepositoryRoot();
        string scriptPath = Path.Combine(repositoryRoot, "tools", "build-image.ps1");
        if (!File.Exists(scriptPath)) {
            throw new FileNotFoundException($"The Xbox 360 native build script was not found at '{scriptPath}'.", scriptPath);
        }

        ProcessStartInfo startInfo = new() {
            FileName = ResolvePowerShell(),
            WorkingDirectory = repositoryRoot,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add("-NoProfile");
        startInfo.ArgumentList.Add("-ExecutionPolicy");
        startInfo.ArgumentList.Add("Bypass");
        startInfo.ArgumentList.Add("-File");
        startInfo.ArgumentList.Add(scriptPath);
        startInfo.ArgumentList.Add("-GeneratedCoreRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(generatedCoreCppRootPath));
        startInfo.ArgumentList.Add("-ContentRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(contentRootPath));
        startInfo.ArgumentList.Add("-CatalogRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(catalogRootPath));
        startInfo.ArgumentList.Add("-OutputRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(outputRootPath));
        startInfo.ArgumentList.Add("-NativeObjectCacheRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(nativeObjectCacheRootPath));

        using Process process = Process.Start(startInfo)
            ?? throw new InvalidOperationException("The Xbox 360 native build process could not be started.");
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        try {
            process.WaitForExit();
        } catch (OperationCanceledException) {
            TryKill(process);
            throw;
        }
        if (cancellationToken.IsCancellationRequested) {
            TryKill(process);
            cancellationToken.ThrowIfCancellationRequested();
        }

        string output = standardOutput.GetAwaiter().GetResult();
        string error = standardError.GetAwaiter().GetResult();
        if (process.ExitCode != 0) {
            throw new InvalidOperationException(
                $"The Xbox 360 native build failed with exit code {process.ExitCode}.{Environment.NewLine}{output}{Environment.NewLine}{error}");
        }

        string imagePath = Path.Combine(Path.GetFullPath(outputRootPath), Xbox360PlatformAssetBuilder.ImageFileName);
        if (!File.Exists(imagePath)) {
            throw new InvalidOperationException(
                $"The Xbox 360 native build reported success but produced no image at '{imagePath}'.{Environment.NewLine}{output}");
        }
    }

    /// <summary>Finds the repository, preferring the environment variable over the layout beside the builder.</summary>
    static string ResolveRepositoryRoot() {
        string configured = Environment.GetEnvironmentVariable(RepositoryRootVariable);
        if (!string.IsNullOrWhiteSpace(configured)) {
            if (!Directory.Exists(configured)) {
                throw new DirectoryNotFoundException($"{RepositoryRootVariable} points at '{configured}', which does not exist.");
            }
            return Path.GetFullPath(configured);
        }
        // The builder assembly lives at <repository>/builder/bin/<configuration>/<framework>.
        string candidate = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        if (Directory.Exists(Path.Combine(candidate, "tools"))) {
            return candidate;
        }
        throw new DirectoryNotFoundException(
            $"The Xbox 360 repository could not be located from '{AppContext.BaseDirectory}'. Set {RepositoryRootVariable}.");
    }

    /// <summary>Prefers PowerShell 7 and falls back to Windows PowerShell.</summary>
    static string ResolvePowerShell() {
        foreach (string candidate in new[] { "pwsh", "powershell" }) {
            try {
                using Process probe = Process.Start(new ProcessStartInfo(candidate, "-NoProfile -Command exit 0") {
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false
                });
                probe?.WaitForExit();
                if (probe != null && probe.ExitCode == 0) {
                    return candidate;
                }
            } catch (Exception) {
                // Try the next candidate; the failure is reported only if none works.
            }
        }
        throw new InvalidOperationException("Neither pwsh nor powershell could be started to run the native build.");
    }

    /// <summary>Kills the build process tree, ignoring a process that has already exited.</summary>
    static void TryKill(Process process) {
        try {
            if (!process.HasExited) {
                process.Kill(true);
            }
        } catch (Exception) {
            // Nothing useful to do if the process is already gone.
        }
    }
}
