using System.Diagnostics;
using System.Text;
using System.Text.Json;
using helengine;

namespace helengine.x360.builder;

/// <summary>Invokes and validates one host protocol process for a single compile request.</summary>
/// <remarks>The include resolver is synchronous; a timeout cannot interrupt arbitrary callback code while it is running.</remarks>
public sealed class Xbox360ShaderCompiler {
    const int MaximumProtocolLineBytes = 8 * 1024 * 1024;
    const int MaximumIncludeRequests = 4096;
    const int MaximumRetainedStandardErrorChars = 64 * 1024;
    static readonly TimeSpan MaximumCleanupWait = TimeSpan.FromSeconds(2);
    static readonly UTF8Encoding StrictUtf8 = new(false, true);
    readonly Xbox360ShaderCompilerOptions Options;

    public Xbox360ShaderCompiler(Xbox360ShaderCompilerOptions options) {
        Options = options ?? throw new ArgumentNullException(nameof(options));
    }

    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        }
        if (includeResolver == null) {
            throw new ArgumentNullException(nameof(includeResolver));
        }

        try {
            Xbox360ShaderBackend.ValidateRequest(request);
            ProcessConfiguration configuration = ResolveProcessConfiguration();
            using JsonDocument response = RunProtocolProcessAsync(request, includeResolver, configuration)
                .GetAwaiter()
                .GetResult();
            return Xbox360ShaderProtocolMapper.Map(request, response.RootElement, Options.VertexLayout);
        } catch (Xbox360ShaderCompilationException) {
            throw;
        } catch (Exception exception) {
            throw new Xbox360ShaderCompilationException(request, exception.Message, exception);
        }
    }

    async Task<JsonDocument> RunProtocolProcessAsync(
        ShaderCompileRequest request,
        IShaderIncludeResolver includeResolver,
        ProcessConfiguration configuration) {
        using CancellationTokenSource timeout = new();
        timeout.CancelAfter(Options.Timeout);

        ProcessStartInfo startInfo = new() {
            FileName = configuration.PythonExecutable,
            WorkingDirectory = configuration.BuilderAssemblyDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardInputEncoding = StrictUtf8,
            StandardOutputEncoding = StrictUtf8,
            StandardErrorEncoding = StrictUtf8
        };
        startInfo.ArgumentList.Add("-u");
        startInfo.ArgumentList.Add(configuration.ProtocolScriptPath);
        startInfo.ArgumentList.Add("--shaderc");
        startInfo.ArgumentList.Add(configuration.ShadercLibraryPath);

        using Process process = new() { StartInfo = startInfo };
        try {
            if (!process.Start()) {
                throw new InvalidOperationException($"Python executable '{configuration.PythonExecutable}' did not start.");
            }
        } catch (Exception exception) {
            throw new InvalidOperationException(
                $"Could not start the Xbox 360 shader protocol process '{configuration.PythonExecutable}': {exception.Message}",
                exception);
        }

        Task<string> standardErrorTask = DrainStandardErrorAsync(process.StandardError);
        BoundedProtocolLineReader output = new(process.StandardOutput.BaseStream, MaximumProtocolLineBytes, StrictUtf8);

        try {
            await WriteProtocolMessageAsync(process.StandardInput.BaseStream, BuildCompileMessage(request, Options.VertexLayout), timeout.Token)
                .WaitAsync(timeout.Token)
                .ConfigureAwait(false);
            int includeCount = 0;
            while (true) {
                string line = await output.ReadLineAsync(timeout.Token).WaitAsync(timeout.Token).ConfigureAwait(false);
                if (line == null) {
                    throw new InvalidDataException("The protocol process reached EOF before sending a final result.");
                }

                JsonDocument message;
                try {
                    message = JsonDocument.Parse(line);
                } catch (JsonException exception) {
                    throw new InvalidDataException($"The protocol process returned malformed JSON: {exception.Message}", exception);
                }

                JsonElement root = message.RootElement;
                try {
                    ValidateProtocolHeader(root);
                    string messageType = RequiredString(root, "type", "protocol message");
                    if (messageType == "include") {
                        try {
                            await HandleIncludeRequestAsync(
                                request,
                                includeResolver,
                                root,
                                ++includeCount,
                                process.StandardInput.BaseStream,
                                timeout.Token).ConfigureAwait(false);
                        } finally {
                            message.Dispose();
                        }
                        continue;
                    }
                    if (messageType != "result") {
                        throw new InvalidDataException($"Unsupported protocol message type '{messageType}'.");
                    }

                    bool success = RequiredBoolean(root, "success", "result");
                    await process.StandardInput.BaseStream.FlushAsync(timeout.Token).WaitAsync(timeout.Token).ConfigureAwait(false);
                    process.StandardInput.Close();
                    await process.WaitForExitAsync(timeout.Token).WaitAsync(timeout.Token).ConfigureAwait(false);

                    string extraLine = await output.ReadLineAsync(timeout.Token).WaitAsync(timeout.Token).ConfigureAwait(false);
                    if (extraLine != null) {
                        throw new InvalidDataException("The protocol process wrote an extra message after its final result.");
                    }

                    string standardError = await standardErrorTask.WaitAsync(timeout.Token).ConfigureAwait(false);
                    if (success && process.ExitCode != 0) {
                        throw new InvalidDataException($"The protocol process returned success but exited with an unexpected exit code {process.ExitCode}.{AppendStandardError(string.Empty, standardError)}");
                    }
                    if (!success) {
                        string diagnostics = OptionalString(root, "diagnostics", "result");
                        string failure = string.IsNullOrWhiteSpace(diagnostics)
                            ? "The Python backend reported a failed compile without diagnostics."
                            : diagnostics;
                        if (process.ExitCode == 0) {
                            failure = $"The protocol process reported failure but exited with code 0. {failure}";
                        }
                        failure = AppendStandardError(failure, standardError);
                        throw new InvalidDataException(failure);
                    }

                    return message;
                } catch {
                    message.Dispose();
                    throw;
                }
            }
        } catch (Exception exception) {
            string cleanupDiagnostic = await StopOwnedChildAsync(process).ConfigureAwait(false);
            string standardError = await GetStandardErrorAsync(standardErrorTask).ConfigureAwait(false);
            string detail = timeout.IsCancellationRequested
                ? $"The shader protocol process exceeded its timeout of {Options.Timeout}."
                : exception.Message;
            detail = AppendStandardError(detail, standardError);
            detail = AppendStandardError(detail, cleanupDiagnostic);
            throw new InvalidDataException(detail, exception);
        } finally {
            if (HasNotExited(process)) {
                _ = await StopOwnedChildAsync(process).ConfigureAwait(false);
            }
        }
    }

    async Task HandleIncludeRequestAsync(
        ShaderCompileRequest request,
        IShaderIncludeResolver includeResolver,
        JsonElement message,
        int includeCount,
        Stream standardInput,
        CancellationToken cancellationToken) {
        if (includeCount > MaximumIncludeRequests) {
            throw new InvalidDataException($"The protocol requested more than {MaximumIncludeRequests} includes.");
        }

        int id = RequiredNonnegativeInteger(message, "id", "include request");
        if (id != includeCount || id == 0) {
            throw new InvalidDataException($"Include request ID {id} did not match expected ID {includeCount}.");
        }

        string requestingSource = RequiredString(message, "requesting_source", "include request");
        string includeName = RequiredString(message, "include_name", "include request");
        Dictionary<string, object> response = new(StringComparer.Ordinal) {
            ["protocol"] = 1,
            ["type"] = "include_result",
            ["id"] = id
        };
        try {
            ShaderIncludeResult include = includeResolver.Resolve(requestingSource, includeName);
            if (include == null) {
                throw new InvalidOperationException("The include resolver returned no result.");
            }
            response["source_name"] = include.Path;
            response["source"] = include.Source;
        } catch (Exception exception) when (!cancellationToken.IsCancellationRequested) {
            response["error"] = string.IsNullOrWhiteSpace(exception.Message)
                ? "The include resolver failed without a diagnostic."
                : exception.Message;
        }

        await WriteProtocolMessageAsync(standardInput, response, cancellationToken)
            .WaitAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    ProcessConfiguration ResolveProcessConfiguration() {
        string assemblyPath = typeof(Xbox360ShaderCompiler).Assembly.Location;
        if (string.IsNullOrWhiteSpace(assemblyPath)) {
            throw new InvalidOperationException("The builder assembly location is unavailable; the default shader protocol path cannot be resolved.");
        }

        string assemblyDirectory = Path.GetDirectoryName(Path.GetFullPath(assemblyPath));
        string protocolScript = Options.ProtocolScriptPath;
        if (string.IsNullOrWhiteSpace(protocolScript)) {
            protocolScript = Path.Combine(assemblyDirectory, "shader-tools", "shader-probe", "backend_protocol.py");
        }
        protocolScript = ResolveAssemblyRelativePath(protocolScript, assemblyDirectory);
        if (!File.Exists(protocolScript)) {
            throw new FileNotFoundException($"The Python protocol script was not found at '{protocolScript}'.", protocolScript);
        }

        string shaderc = Options.ShadercLibraryPath;
        if (string.IsNullOrWhiteSpace(shaderc)) {
            shaderc = FirstNonemptyEnvironmentValue("HEL_X360_SHADERC", "SHADERC_SHARED_LIBRARY");
        }
        if (string.IsNullOrWhiteSpace(shaderc)) {
            throw new InvalidOperationException("Set HEL_X360_SHADERC or SHADERC_SHARED_LIBRARY to the shaderc_shared.dll path.");
        }
        shaderc = ResolveAssemblyRelativePath(shaderc, assemblyDirectory);
        if (!File.Exists(shaderc)) {
            throw new FileNotFoundException($"The shaderc shared library was not found at '{shaderc}'.", shaderc);
        }

        string python = Options.PythonExecutable;
        if (string.IsNullOrWhiteSpace(python)) {
            python = Environment.GetEnvironmentVariable("HEL_X360_PYTHON");
        }
        if (string.IsNullOrWhiteSpace(python)) {
            python = "python";
        }

        bool hasDirectory = Path.IsPathRooted(python)
            || python.IndexOf(Path.DirectorySeparatorChar) >= 0
            || python.IndexOf(Path.AltDirectorySeparatorChar) >= 0;
        if (hasDirectory) {
            python = ResolveAssemblyRelativePath(python, assemblyDirectory);
            if (!File.Exists(python)) {
                throw new FileNotFoundException($"The Python executable was not found at '{python}'.", python);
            }
        }

        return new ProcessConfiguration(assemblyDirectory, python, protocolScript, shaderc);
    }

    static string ResolveAssemblyRelativePath(string path, string assemblyDirectory) =>
        Path.GetFullPath(path, assemblyDirectory);

    static string FirstNonemptyEnvironmentValue(string firstName, string secondName) {
        string first = Environment.GetEnvironmentVariable(firstName);
        if (!string.IsNullOrWhiteSpace(first)) {
            return first;
        }
        string second = Environment.GetEnvironmentVariable(secondName);
        return string.IsNullOrWhiteSpace(second) ? null : second;
    }

    static object BuildCompileMessage(ShaderCompileRequest request, Xbox360ShaderVertexLayout vertexLayout) {
        Dictionary<string, string> defines = new(StringComparer.Ordinal);
        foreach (ShaderDefine define in request.Defines) {
            if (!defines.TryAdd(define.Name, define.Value)) {
                throw new InvalidOperationException($"Duplicate define name '{define.Name}' cannot be represented by the protocol define map.");
            }
        }

        ShaderCompileOptions options = request.Options;
        ShaderBindingPolicy policy = options.BindingPolicy;
        return new {
            protocol = 1,
            type = "compile",
            source = request.Source.Source,
            source_name = request.Source.Path,
            stage = GetStageName(request.Stage),
            entrypoint = request.EntryPoint,
            program = request.ProgramName,
            variant = request.Variant,
            target = "x360",
            shader_model = "4.0",
            vertex_layout = Xbox360ShaderVertexLayouts.ToProtocolValue(vertexLayout),
            defines,
            options = new {
                debug_info = options.GenerateDebugInfo,
                optimize = options.Optimize,
                warnings_as_errors = options.TreatWarningsAsErrors
            },
            binding_policy = new {
                default_space = policy.DefaultSpace,
                constant_buffer_shift = policy.ConstantBufferShift,
                texture_shift = policy.TextureShift,
                sampler_shift = policy.SamplerShift,
                storage_shift = policy.StorageShift
            }
        };
    }

    static string GetStageName(ShaderStage stage) => stage switch {
        ShaderStage.Vertex => "vertex",
        ShaderStage.Pixel => "pixel",
        _ => throw new InvalidOperationException($"Unsupported shader stage '{stage}'.")
    };

    static async Task WriteProtocolMessageAsync(Stream stream, object message, CancellationToken cancellationToken) {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(message);
        if (payload.Length + 1 > MaximumProtocolLineBytes) {
            throw new InvalidDataException($"A protocol message exceeds the {MaximumProtocolLineBytes} UTF-8 byte limit.");
        }

        byte[] line = new byte[payload.Length + 1];
        Buffer.BlockCopy(payload, 0, line, 0, payload.Length);
        line[^1] = (byte)'\n';
        await stream.WriteAsync(line, 0, line.Length, cancellationToken).WaitAsync(cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    static async Task<string> DrainStandardErrorAsync(StreamReader reader) {
        char[] buffer = new char[4096];
        StringBuilder retained = new();
        long totalChars = 0;
        while (true) {
            int read = await reader.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false);
            if (read == 0) {
                break;
            }

            totalChars += read;
            int retainCount = Math.Min(read, MaximumRetainedStandardErrorChars - retained.Length);
            if (retainCount > 0) {
                retained.Append(buffer, 0, retainCount);
            }
        }

        if (totalChars > retained.Length) {
            retained.Append($"\n[stderr truncated after {MaximumRetainedStandardErrorChars} characters]");
        }
        return retained.ToString();
    }

    static async Task<string> GetStandardErrorAsync(Task<string> standardErrorTask) {
        try {
            return await standardErrorTask.WaitAsync(MaximumCleanupWait).ConfigureAwait(false);
        } catch (TimeoutException) {
            return "[stderr drain did not finish before cleanup deadline]";
        } catch (Exception exception) {
            return $"[stderr could not be read: {exception.Message}]";
        }
    }

    static string AppendStandardError(string diagnostic, string standardError) {
        if (string.IsNullOrWhiteSpace(standardError)) {
            return diagnostic;
        }
        return string.IsNullOrWhiteSpace(diagnostic)
            ? $"stderr: {standardError}"
            : $"{diagnostic}\nstderr: {standardError}";
    }

    static void ValidateProtocolHeader(JsonElement message) {
        if (message.ValueKind != JsonValueKind.Object) {
            throw new InvalidDataException("A protocol message must be a JSON object.");
        }
        int protocol = RequiredNonnegativeInteger(message, "protocol", "protocol message");
        if (protocol != 1) {
            throw new InvalidDataException($"Unsupported protocol version {protocol}; expected 1.");
        }
        _ = RequiredString(message, "type", "protocol message");
    }

    static string RequiredString(JsonElement value, string propertyName, string owner) {
        if (value.ValueKind != JsonValueKind.Object || !value.TryGetProperty(propertyName, out JsonElement property)) {
            throw new InvalidDataException($"{owner} is missing required field '{propertyName}'.");
        }
        if (property.ValueKind != JsonValueKind.String) {
            throw new InvalidDataException($"{owner}.{propertyName} must be a string.");
        }
        string result = property.GetString();
        if (string.IsNullOrWhiteSpace(result)) {
            throw new InvalidDataException($"{owner}.{propertyName} must be nonempty.");
        }
        return result;
    }

    static string OptionalString(JsonElement value, string propertyName, string owner) {
        if (value.ValueKind != JsonValueKind.Object || !value.TryGetProperty(propertyName, out JsonElement property)) {
            throw new InvalidDataException($"{owner} is missing required field '{propertyName}'.");
        }
        if (property.ValueKind != JsonValueKind.String) {
            throw new InvalidDataException($"{owner}.{propertyName} must be a string.");
        }
        return property.GetString();
    }

    static bool RequiredBoolean(JsonElement value, string propertyName, string owner) {
        if (value.ValueKind != JsonValueKind.Object || !value.TryGetProperty(propertyName, out JsonElement property)) {
            throw new InvalidDataException($"{owner} is missing required field '{propertyName}'.");
        }
        if (property.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) {
            throw new InvalidDataException($"{owner}.{propertyName} must be a boolean.");
        }
        return property.GetBoolean();
    }

    static int RequiredNonnegativeInteger(JsonElement value, string propertyName, string owner) {
        if (value.ValueKind != JsonValueKind.Object || !value.TryGetProperty(propertyName, out JsonElement property)) {
            throw new InvalidDataException($"{owner} is missing required field '{propertyName}'.");
        }
        if (property.ValueKind != JsonValueKind.Number || !property.TryGetInt32(out int result) || result < 0) {
            throw new InvalidDataException($"{owner}.{propertyName} must be a nonnegative 32-bit integer.");
        }
        return result;
    }

    static bool HasNotExited(Process process) {
        try {
            return !process.HasExited;
        } catch (InvalidOperationException) {
            return false;
        }
    }

    static async Task<string> StopOwnedChildAsync(Process process) {
        string killError = null;
        try {
            if (HasNotExited(process)) {
                process.Kill(entireProcessTree: false);
            }
        } catch (InvalidOperationException) {
            killError = "The protocol child exited while cleanup was starting.";
        } catch (System.ComponentModel.Win32Exception exception) {
            killError = $"Could not terminate the protocol child: {exception.Message}";
        }

        try {
            if (HasNotExited(process)) {
                await process.WaitForExitAsync().WaitAsync(MaximumCleanupWait).ConfigureAwait(false);
            }
        } catch (TimeoutException) {
            string timeout = $"The owned protocol child did not exit within {MaximumCleanupWait.TotalSeconds:0} seconds after termination was requested.";
            return string.IsNullOrWhiteSpace(killError) ? timeout : $"{killError} {timeout}";
        } catch (InvalidOperationException) {
            // The process handle has already been closed.
        } catch (System.ComponentModel.Win32Exception exception) {
            string waitError = $"Could not confirm protocol child exit: {exception.Message}";
            return string.IsNullOrWhiteSpace(killError) ? waitError : $"{killError} {waitError}";
        }
        return killError;
    }

    sealed record ProcessConfiguration(
        string BuilderAssemblyDirectory,
        string PythonExecutable,
        string ProtocolScriptPath,
        string ShadercLibraryPath);

    sealed class BoundedProtocolLineReader {
        readonly Stream Input;
        readonly int MaximumLineBytes;
        readonly UTF8Encoding Encoding;
        readonly byte[] Buffer = new byte[8192];
        int Offset;
        int Count;

        public BoundedProtocolLineReader(Stream input, int maximumLineBytes, UTF8Encoding encoding) {
            Input = input;
            MaximumLineBytes = maximumLineBytes;
            Encoding = encoding;
        }

        public async Task<string> ReadLineAsync(CancellationToken cancellationToken) {
            using MemoryStream line = new();
            while (true) {
                if (Offset == Count) {
                    Count = await Input.ReadAsync(Buffer, 0, Buffer.Length, cancellationToken)
                        .WaitAsync(cancellationToken)
                        .ConfigureAwait(false);
                    Offset = 0;
                    if (Count == 0) {
                        if (line.Length == 0) {
                            return null;
                        }
                        throw new InvalidDataException("The protocol process ended with a truncated message before newline.");
                    }
                }

                int available = Count - Offset;
                int newline = Array.IndexOf(Buffer, (byte)'\n', Offset, available);
                int end = newline >= 0 ? newline : Count;
                int length = end - Offset;
                if (line.Length + length + 1 > MaximumLineBytes) {
                    throw new InvalidDataException($"A protocol message exceeds the {MaximumLineBytes} UTF-8 byte limit (8MiB).");
                }
                if (length > 0) {
                    line.Write(Buffer, Offset, length);
                }
                Offset = newline >= 0 ? newline + 1 : Count;
                if (newline < 0) {
                    continue;
                }

                byte[] bytes = line.ToArray();
                int byteCount = bytes.Length;
                if (byteCount > 0 && bytes[byteCount - 1] == (byte)'\r') {
                    byteCount--;
                }
                try {
                    return Encoding.GetString(bytes, 0, byteCount);
                } catch (DecoderFallbackException exception) {
                    throw new InvalidDataException($"A protocol message is not valid UTF-8: {exception.Message}", exception);
                }
            }
        }
    }
}
