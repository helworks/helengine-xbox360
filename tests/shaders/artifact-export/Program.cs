using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using helengine;
using helengine.x360.builder;

internal static class Program {
    const string MeshAssetId = "task4/legacy-mesh3d";
    const string BatchUnoptimizedAssetId = "task4/shared-batch2d-textured-unoptimized";
    const string BatchOptimizedAssetId = "task4/shared-batch2d-textured-optimized";
    static readonly UTF8Encoding Utf8 = new(false, true);

    static int Main(string[] args) {
        try {
            Arguments parsed = Arguments.Parse(args);
            string repositoryRoot = FindRepositoryRoot(Environment.CurrentDirectory);
            string engineRoot = Path.GetFullPath(parsed.EngineRoot);
            string output = Path.GetFullPath(parsed.Output);
            ValidateInputs(repositoryRoot, engineRoot, output, parsed.Shaderc);

            Xbox360ShaderArtifact mesh = CompileMesh(repositoryRoot, parsed.Shaderc);
            StandardShaderVariant textured = SpriteBatchShaderVariants.All
                .SingleOrDefault(variant => string.Equals(variant.Name, "Textured", StringComparison.Ordinal))
                ?? throw new InvalidOperationException("The shared SpriteBatch catalog has no Textured variant.");
            string batchPath = Path.Combine(engineRoot, "engine", "helengine.editor", "shaders", "builtin", "SpriteBatchShader.hlsl");
            string batchSource = File.ReadAllText(batchPath, Utf8);
            Xbox360ShaderArtifact batchUnoptimized = CompileBatch(batchPath, batchSource, textured, parsed.Shaderc, optimize: false);
            Xbox360ShaderArtifact batchOptimized = CompileBatch(batchPath, batchSource, textured, parsed.Shaderc, optimize: true);

            RequireCosts(mesh, 110, "legacy Mesh3D");
            RequireBatchCounts(batchUnoptimized);
            RequireBatchCounts(batchOptimized);
            RequireCosts(batchUnoptimized, 94, "unoptimized shared Batch2D Textured");
            RequireCosts(batchOptimized, 94, "optimized shared Batch2D Textured");

            WriteArtifact(output, "mesh.x3sp", mesh);
            WriteArtifact(output, "batch-unoptimized.x3sp", batchUnoptimized);
            WriteArtifact(output, "batch-optimized.x3sp", batchOptimized);
            WriteManifest(output, [
                ("mesh.x3sp", mesh),
                ("batch-unoptimized.x3sp", batchUnoptimized),
                ("batch-optimized.x3sp", batchOptimized)
            ]);
            Console.WriteLine("Exported mesh.x3sp, batch-unoptimized.x3sp, batch-optimized.x3sp, and expected.txt");
            Console.WriteLine($"Mesh3D upload cost: {mesh.Programs.PairUploadDwords}; Batch2D costs: {batchUnoptimized.Programs.PairUploadDwords}/{batchOptimized.Programs.PairUploadDwords}");
            return 0;
        } catch (Exception exception) {
            Console.Error.WriteLine($"Artifact export failed: {exception.Message}");
            return 1;
        }
    }

    static Xbox360ShaderArtifact CompileMesh(string repositoryRoot, string shaderc) {
        string fixtureDirectory = Path.Combine(repositoryRoot, "tests", "shaders", "fixtures");
        Xbox360ShaderProgram vertex = CompileStage(
            Path.Combine(fixtureDirectory, "transform_vs.hlsl"), "task4-mesh-vs", "main",
            "Task4LegacyMesh3D", ShaderStage.Vertex, Xbox360ShaderVertexLayout.Mesh3D,
            optimize: false, shaderc);
        Xbox360ShaderProgram pixel = CompileStage(
            Path.Combine(fixtureDirectory, "texture_ps.hlsl"), "task4-mesh-ps", "main",
            "Task4LegacyMesh3D", ShaderStage.Pixel, Xbox360ShaderVertexLayout.Mesh3D,
            optimize: false, shaderc);
        return new Xbox360ShaderArtifact(MeshAssetId, Xbox360ShaderProgramPair.Link(vertex, pixel));
    }

    static Xbox360ShaderArtifact CompileBatch(
        string sourcePath,
        string source,
        StandardShaderVariant variant,
        string shaderc,
        bool optimize) {
        Xbox360ShaderProgram vertex = CompileStage(
            sourcePath, "SpriteBatch.Textured.VS", variant.VertexEntryPoint,
            variant.Name, ShaderStage.Vertex, Xbox360ShaderVertexLayout.Batch2D,
            optimize, shaderc, source, variant.Defines);
        Xbox360ShaderProgram pixel = CompileStage(
            sourcePath, "SpriteBatch.Textured.PS", variant.PixelEntryPoint,
            variant.Name, ShaderStage.Pixel, Xbox360ShaderVertexLayout.Batch2D,
            optimize, shaderc, source, variant.Defines);
        string assetId = optimize ? BatchOptimizedAssetId : BatchUnoptimizedAssetId;
        return new Xbox360ShaderArtifact(assetId, Xbox360ShaderProgramPair.Link(vertex, pixel));
    }

    static Xbox360ShaderProgram CompileStage(
        string sourcePath,
        string programName,
        string entryPoint,
        string variant,
        ShaderStage stage,
        Xbox360ShaderVertexLayout layout,
        bool optimize,
        string shaderc,
        string sourceOverride = null,
        IReadOnlyList<string> catalogDefines = null) {
        string source = sourceOverride ?? File.ReadAllText(sourcePath, Utf8);
        ShaderDefine[] defines = (catalogDefines ?? Array.Empty<string>())
            .Select(ParseDefine)
            .ToArray();
        ShaderCompileRequest request = new(
            new ShaderSourceInfo(sourcePath, source),
            programName,
            entryPoint,
            stage,
            ShaderCompileTarget.Xbox360,
            new ShaderModel(4, 0),
            variant,
            defines,
            new ShaderCompileOptions(new ShaderBindingPolicy(0, 17, 101, 202, 300), false, optimize, false));
        Xbox360ShaderBackend backend = new(new Xbox360ShaderCompilerOptions(
            shadercLibraryPath: shaderc,
            timeout: TimeSpan.FromSeconds(60),
            vertexLayout: layout));
        ShaderCompileResult result = backend.Compile(request, NoIncludes.Instance);
        return Xbox360ShaderProgramFactory.FromCompilation(result as Xbox360ShaderCompileResult
            ?? throw new InvalidDataException("The Xbox 360 compiler returned a non-Xbox360 result."));
    }

    static ShaderDefine ParseDefine(string define) {
        int separator = define.IndexOf('=');
        if (separator <= 0 || separator == define.Length - 1) {
            throw new InvalidDataException($"Shared variant define '{define}' is not a NAME=VALUE pair.");
        }
        return new ShaderDefine(define[..separator], define[(separator + 1)..]);
    }

    static void RequireCosts(Xbox360ShaderArtifact artifact, int expected, string label) {
        if (artifact.Programs.PairUploadDwords != expected) {
            throw new InvalidDataException($"{label} upload cost was {artifact.Programs.PairUploadDwords}, expected {expected} dwords.");
        }
    }

    static void RequireBatchCounts(Xbox360ShaderArtifact artifact) {
        if (artifact.Programs.Vertex.MicrocodeWords.Count != 45 || artifact.Programs.Pixel.MicrocodeWords.Count != 15) {
            throw new InvalidDataException(
                $"Batch2D microcode word counts were {artifact.Programs.Vertex.MicrocodeWords.Count}/{artifact.Programs.Pixel.MicrocodeWords.Count}, expected 45/15.");
        }
    }

    static void WriteArtifact(string output, string fileName, Xbox360ShaderArtifact artifact) {
        byte[] bytes = Xbox360ShaderArtifactSerializer.Serialize(artifact);
        using FileStream stream = new(Path.Combine(output, fileName), FileMode.CreateNew, FileAccess.Write, FileShare.None);
        stream.Write(bytes);
    }

    static void WriteManifest(string output, (string Name, Xbox360ShaderArtifact Artifact)[] artifacts) {
        StringBuilder manifest = new();
        foreach ((string fileName, Xbox360ShaderArtifact artifact) in artifacts) {
            Xbox360ShaderProgramPair pair = artifact.Programs;
            manifest.Append("FILE\t").Append(fileName).Append('\n');
            manifest.Append("PAIR\t").Append(artifact.AssetId).Append('\t')
                .Append(pair.Vertex.Variant).Append('\t').Append(pair.Vertex.ToolIdentity.ToLowerInvariant()).Append('\n');
            manifest.Append("COST\t").Append(pair.ProgramControl.ToString(CultureInfo.InvariantCulture)).Append('\t')
                .Append(pair.InterpolatorCount.ToString(CultureInfo.InvariantCulture)).Append('\t')
                .Append(pair.ProgramUploadDwords.ToString(CultureInfo.InvariantCulture)).Append('\t')
                .Append(pair.ConstantUploadDwords.ToString(CultureInfo.InvariantCulture)).Append('\t')
                .Append(pair.StateUploadDwords.ToString(CultureInfo.InvariantCulture)).Append('\t')
                .Append(pair.PairUploadDwords.ToString(CultureInfo.InvariantCulture)).Append('\n');
            WriteStageManifest(manifest, 0, pair.Vertex);
            WriteStageManifest(manifest, 1, pair.Pixel);
        }

        using FileStream stream = new(Path.Combine(output, "expected.txt"), FileMode.CreateNew, FileAccess.Write, FileShare.None);
        byte[] contents = Utf8.GetBytes(manifest.ToString());
        stream.Write(contents);
    }

    static void WriteStageManifest(StringBuilder output, int stageIndex, Xbox360ShaderProgram program) {
        Xbox360ShaderConstantBufferLayout buffer = program.ConstantBuffer;
        int bufferSize = buffer?.SizeBytes ?? 0;
        int capacity = buffer?.RegisterCapacity ?? 0;
        int bank = buffer?.PhysicalBankBaseRegister ?? -1;
        output.Append("STAGE\t").Append(stageIndex).Append('\t').Append(StageCode(program.Stage)).Append('\t')
            .Append(ProfileCode(program.VertexLayout)).Append('\t').Append(program.RegisterCount).Append('\t')
            .Append(program.MicrocodeWords.Count).Append('\t').Append(bufferSize).Append('\t').Append(capacity).Append('\t')
            .Append(bank).Append('\t').Append(program.ProgramName).Append('\t').Append(program.EntryPoint).Append('\t')
            .Append(program.CompileIdentity.ToLowerInvariant()).Append('\t').Append(program.BytecodeSha256.ToLowerInvariant()).Append('\n');

        int wordIndex = 0;
        foreach (uint word in program.MicrocodeWords) {
            output.Append("WORD\t").Append(stageIndex).Append('\t')
                .Append(wordIndex++).Append('\t').Append(word.ToString("x8", CultureInfo.InvariantCulture)).Append('\n');
        }
        WriteInterfaces(output, stageIndex, "I", program.Inputs);
        WriteInterfaces(output, stageIndex, "O", program.Outputs);
        for (int index = 0; index < program.Resources.Count; index++) {
            Xbox360ShaderResource resource = program.Resources[index];
            output.Append("RESOURCE\t").Append(stageIndex).Append('\t').Append(index).Append('\t')
                .Append(ResourceCode(resource.Type)).Append('\t').Append(resource.Name).Append('\t')
                .Append(resource.LogicalSet).Append('\t').Append(resource.LogicalSlot).Append('\t')
                .Append(resource.PhysicalSet).Append('\t').Append(resource.PhysicalBinding).Append('\t')
                .Append(Optional(resource.PhysicalSlot)).Append('\t').Append(Optional(resource.PhysicalBankBaseRegister)).Append('\n');
        }
        if (buffer == null) return;
        foreach (Xbox360ShaderDeclaredConstantMember member in buffer.DeclaredMembers) {
            output.Append("DECL\t").Append(stageIndex).Append('\t').Append(MemberCode(member.Type)).Append('\t')
                .Append(member.Name).Append('\t').Append(member.OffsetBytes).Append('\t').Append(member.SizeBytes).Append('\n');
        }
        foreach (Xbox360ShaderUsedConstantMember member in buffer.UsedMembers) {
            output.Append("USED\t").Append(stageIndex).Append('\t').Append(member.Name).Append('\t')
                .Append(member.OffsetBytes).Append('\t').Append(member.RegisterIndices[0]).Append('\t')
                .Append(member.RegisterIndices.Count).Append('\t').Append(MatrixCode(member.MatrixMajor)).Append('\t')
                .Append(Optional(member.MatrixStrideBytes)).Append('\n');
        }
    }

    static void WriteInterfaces(StringBuilder output, int stage, string direction, IReadOnlyList<Xbox360ShaderInterface> items) {
        foreach (Xbox360ShaderInterface item in items) {
            Xbox360ShaderVertexFetchLayout fetch = item.VertexFetch;
            // The wire has no varying slot for PS outputs. The factory's PS
            // output PhysicalSlot is compiler/factory metadata, not a wire field.
            int? wireSlot = stage == 1 && direction == "O" ? null : item.PhysicalSlot;
            output.Append("INTERFACE\t").Append(stage).Append('\t').Append(direction).Append('\t')
                .Append(InterfaceCode(item.Kind)).Append('\t').Append(item.Index).Append('\t').Append(item.Width).Append('\t')
                .Append(Optional(wireSlot)).Append('\t').Append(fetch == null ? -1 : fetch.FetchConstant).Append('\t')
                .Append(fetch?.Stride ?? 0).Append('\t').Append(fetch?.Offset ?? 0).Append('\t')
                .Append(fetch == null ? 0 : FetchFormatCode(fetch.HardwareFormat)).Append('\n');
        }
    }

    static int StageCode(ShaderStage stage) => stage switch { ShaderStage.Vertex => 1, ShaderStage.Pixel => 2, _ => throw new InvalidDataException("Unsupported shader stage.") };
    static int ProfileCode(Xbox360ShaderVertexLayout profile) => profile switch { Xbox360ShaderVertexLayout.Mesh3D => 1, Xbox360ShaderVertexLayout.Batch2D => 2, _ => throw new InvalidDataException("Unsupported vertex profile.") };
    static int InterfaceCode(Xbox360ShaderInterfaceKind kind) => kind switch { Xbox360ShaderInterfaceKind.Location => 1, Xbox360ShaderInterfaceKind.Position => 2, _ => throw new InvalidDataException("Unsupported interface kind.") };
    static int ResourceCode(ShaderResourceType type) => type switch { ShaderResourceType.ConstantBuffer => 1, ShaderResourceType.Texture2D => 2, ShaderResourceType.Sampler => 3, _ => throw new InvalidDataException("Unsupported resource type.") };
    static int MemberCode(string type) => type switch { "float" => 1, "float2" => 2, "float3" => 3, "float4" => 4, "float4x4" => 5, _ => throw new InvalidDataException($"Unsupported constant type '{type}'.") };
    static int MatrixCode(string major) => major switch { null => 0, "row_major" => 1, "column_major" => 2, _ => throw new InvalidDataException($"Unsupported matrix orientation '{major}'.") };
    static int FetchFormatCode(string format) => format switch { "32_32_FLOAT" => 2, "32_32_32_FLOAT" => 3, "32_32_32_32_FLOAT" => 4, _ => throw new InvalidDataException($"Unsupported fetch format '{format}'.") };
    static int Optional(int? value) => value ?? -1;

    static void ValidateInputs(string repositoryRoot, string engineRoot, string output, string shaderc) {
        string sharedProject = Path.Combine(engineRoot, "engine", "helengine.shader.compilation", "helengine.shader.compilation.csproj");
        string sharedCatalog = Path.Combine(engineRoot, "engine", "helengine.shader", "shaders", "SpriteBatchShaderVariants.cs");
        string sharedSource = Path.Combine(engineRoot, "engine", "helengine.editor", "shaders", "builtin", "SpriteBatchShader.hlsl");
        string fixtureVs = Path.Combine(repositoryRoot, "tests", "shaders", "fixtures", "transform_vs.hlsl");
        string fixturePs = Path.Combine(repositoryRoot, "tests", "shaders", "fixtures", "texture_ps.hlsl");
        foreach (string prerequisite in new[] { sharedProject, sharedCatalog, sharedSource, fixtureVs, fixturePs, shaderc }) {
            if (!File.Exists(prerequisite)) throw new FileNotFoundException("Required shader artifact integration source is missing.", prerequisite);
        }
        string prefix = repositoryRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!output.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Output directory must remain inside the checkout: {output}");
        }
        if (!Directory.Exists(output)) throw new DirectoryNotFoundException($"Output directory must already exist: {output}");
        if (Directory.EnumerateFileSystemEntries(output).Any()) {
            throw new IOException($"Output directory must be empty; refusing to overwrite existing files: {output}");
        }
    }

    static string FindRepositoryRoot(string start) {
        for (DirectoryInfo directory = new(Path.GetFullPath(start)); directory != null; directory = directory.Parent) {
            if (File.Exists(Path.Combine(directory.FullName, "builder", "helengine.x360.builder.csproj"))) return directory.FullName;
        }
        throw new DirectoryNotFoundException("Could not locate the helengine-x360 checkout from the current directory.");
    }

    sealed record Arguments(string Output, string Shaderc, string EngineRoot) {
        public static Arguments Parse(string[] args) {
            Dictionary<string, string> values = new(StringComparer.Ordinal);
            for (int index = 0; index < args.Length; index += 2) {
                if (index + 1 >= args.Length || args[index] is not ("--output" or "--shaderc" or "--engine-root")
                    || !values.TryAdd(args[index], args[index + 1])) {
                    throw new ArgumentException("Usage: --output <existing checkout directory> --shaderc <DLL> --engine-root <shared checkout>");
                }
            }
            if (values.Count != 3) throw new ArgumentException("All of --output, --shaderc, and --engine-root are required.");
            return new Arguments(Path.GetFullPath(values["--output"]), Path.GetFullPath(values["--shaderc"]), Path.GetFullPath(values["--engine-root"]));
        }
    }

    sealed class NoIncludes : IShaderIncludeResolver {
        public static readonly NoIncludes Instance = new();
        public ShaderIncludeResult Resolve(string requestingFile, string includePath) =>
            throw new InvalidOperationException($"Unexpected include '{includePath}' requested by '{requestingFile}'.");
    }
}
