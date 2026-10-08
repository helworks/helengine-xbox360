using System.Buffers.Binary;
using System.Reflection;
using System.Security.Cryptography;
using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

internal static class Xbox360ShaderArtifactTestSupport {
    static readonly Type ProgramType = typeof(Xbox360ShaderProgram);
    static readonly Type InterfaceType = typeof(Xbox360ShaderInterface);
    static readonly ConstructorInfo ProgramConstructor = ProgramType.GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic)
        .Single(constructor => constructor.GetParameters().Length == 17);
    static readonly ConstructorInfo InterfaceConstructor = InterfaceType.GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic)
        .Single(constructor => constructor.GetParameters().Length == 6);
    static readonly ConstructorInfo FetchConstructor = typeof(Xbox360ShaderVertexFetchLayout)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single();
    static readonly ConstructorInfo ResourceConstructor = typeof(Xbox360ShaderResource)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single();
    static readonly ConstructorInfo DeclaredConstructor = typeof(Xbox360ShaderDeclaredConstantMember)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single();
    static readonly ConstructorInfo UsedConstructor = typeof(Xbox360ShaderUsedConstantMember)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single();
    static readonly ConstructorInfo BufferConstructor = typeof(Xbox360ShaderConstantBufferLayout)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single();

    public static string GoldenPath => Path.Combine(FindRoot(), "tests", "shaders", "artifacts", "minimal-v1.x3sp");

    public static Xbox360ShaderArtifact MinimalArtifact() {
        Xbox360ShaderProgram vertex = Program(
            "VS", "VS", ShaderStage.Vertex, Xbox360ShaderVertexLayout.Mesh3D,
            "1111111111111111111111111111111111111111111111111111111111111111",
            [0x12345678, 0x01020304, 0xA0B0C0D0],
            MakeInterface(Xbox360ShaderInterfaceKind.Position, 0, 4));
        Xbox360ShaderProgram pixel = Program(
            "PS", "PS", ShaderStage.Pixel, Xbox360ShaderVertexLayout.Mesh3D,
            "2222222222222222222222222222222222222222222222222222222222222222",
            [0x87654321, 0x04030201, 0x0D0C0B0A],
            MakeInterface(Xbox360ShaderInterfaceKind.Location, 0, 4));
        return new Xbox360ShaderArtifact("fixture", Xbox360ShaderProgramPair.Link(vertex, pixel));
    }

    public static Xbox360ShaderArtifact RichArtifact(bool reverseCollections = false) {
        const string tool = "0000000000000000000000000000000000000000000000000000000000000000";
        Xbox360ShaderDeclaredConstantMember vsUsed = Declared("world", "float4x4", 0, 64);
        Xbox360ShaderDeclaredConstantMember vsRowMajor = Declared("view", "float4x4", 64, 64);
        Xbox360ShaderDeclaredConstantMember vsUnusedMatrix = Declared("unusedMatrix", "float4x4", 128, 64);
        Xbox360ShaderConstantBufferLayout vsBuffer = Buffer(192, 0,
            reverseCollections ? [vsUnusedMatrix, vsRowMajor, vsUsed] : [vsUsed, vsRowMajor, vsUnusedMatrix],
            [Used("world", 0, [0, 1, 2, 3], "column_major", 16),
                Used("view", 64, [4, 5, 6, 7], "row_major", 16)]);
        Xbox360ShaderDeclaredConstantMember psMember = Declared("tint", "float4", 0, 16);
        Xbox360ShaderConstantBufferLayout psBuffer = Buffer(16, 256, [psMember],
            [Used("tint", 0, [0], null, null)]);

        Xbox360ShaderResource vsCbuffer = Resource("vsConstants", ShaderResourceType.ConstantBuffer, 0, 17, 0, 0, null, 0);
        Xbox360ShaderResource psCbuffer = Resource("psConstants", ShaderResourceType.ConstantBuffer, 0, 300, 0, 0, null, 256);
        Xbox360ShaderResource texture = Resource("albedo", ShaderResourceType.Texture2D, 2, 301, 2, 17, 1, null);
        Xbox360ShaderResource sampler = Resource("linearSampler", ShaderResourceType.Sampler, 2, 302, 2, 23, 1, null);

        Xbox360ShaderInterface vsInput = Interface(Xbox360ShaderInterfaceKind.Location, 2, 2, null,
            Fetch(0, 12, 6, "32_32_FLOAT"));
        Xbox360ShaderInterface vsPosition = Interface(Xbox360ShaderInterfaceKind.Position, 0, 4, null, null);
        Xbox360ShaderInterface vsVarying = Interface(Xbox360ShaderInterfaceKind.Location, 2, 2, 2, null);
        Xbox360ShaderInterface psInput = Interface(Xbox360ShaderInterfaceKind.Location, 2, 2, 2, null);
        Xbox360ShaderInterface psOutput = Interface(Xbox360ShaderInterfaceKind.Location, 0, 4, null, null);

        Xbox360ShaderProgram vertex = ProgramWith("meshπ", "vsMain", ShaderStage.Vertex, Xbox360ShaderVertexLayout.Mesh3D,
            tool, "1111111111111111111111111111111111111111111111111111111111111111",
            [0x12345678, 0x01020304, 0xA0B0C0D0],
            [vsInput], [vsPosition, vsVarying], [vsCbuffer], vsBuffer);
        Xbox360ShaderProgram pixel = ProgramWith("pixelΩ", "psMain", ShaderStage.Pixel, Xbox360ShaderVertexLayout.Batch2D,
            tool, "2222222222222222222222222222222222222222222222222222222222222222",
            [0x87654321, 0x04030201, 0x0D0C0B0A],
            [psInput], [psOutput],
            reverseCollections ? [sampler, psCbuffer, texture] : [psCbuffer, texture, sampler], psBuffer);
        return new Xbox360ShaderArtifact("asset/π", Xbox360ShaderProgramPair.Link(vertex, pixel));
    }

    public static Xbox360ShaderProgram CloneProgram(
        Xbox360ShaderProgram source,
        string programName = null,
        string entryPoint = null,
        string variant = null) => ProgramWith(
            programName ?? source.ProgramName, entryPoint ?? source.EntryPoint, source.Stage, source.VertexLayout,
            source.ToolIdentity, source.CompileIdentity, source.MicrocodeWords.ToArray(),
            source.Inputs.ToArray(), source.Outputs.ToArray(), source.Resources.ToArray(), source.ConstantBuffer, variant ?? source.Variant);

    public static Xbox360ShaderProgram Snapshot(
        Xbox360ShaderProgram source,
        uint[] words = null,
        Xbox360ShaderInterface[] inputs = null,
        Xbox360ShaderInterface[] outputs = null,
        Xbox360ShaderResource[] resources = null,
        Xbox360ShaderConstantBufferLayout constantBuffer = null,
        bool replaceConstantBuffer = false) => ProgramWith(
            source.ProgramName, source.EntryPoint, source.Stage, source.VertexLayout,
            source.ToolIdentity, source.CompileIdentity, words ?? source.MicrocodeWords.ToArray(),
            inputs ?? source.Inputs.ToArray(), outputs ?? source.Outputs.ToArray(),
            resources ?? source.Resources.ToArray(), replaceConstantBuffer ? constantBuffer : source.ConstantBuffer,
            source.Variant);

    public static Xbox360ShaderArtifact Artifact(Xbox360ShaderProgram vertex, Xbox360ShaderProgram pixel) =>
        new("bounds-test", Xbox360ShaderProgramPair.Link(vertex, pixel));

    public static Xbox360ShaderInterface MakeInterface(
        Xbox360ShaderInterfaceKind kind, int index, int width, int? slot = null,
        Xbox360ShaderVertexFetchLayout fetch = null) => Interface(kind, index, width, slot, fetch);

    public static Xbox360ShaderResource MakeResource(
        string name, ShaderResourceType type, int logicalSet = 0, int logicalSlot = 0,
        int physicalSet = 0, int binding = 0, int? slot = null, int? bank = null) =>
        Resource(name, type, logicalSet, logicalSlot, physicalSet, binding, slot, bank);

    public static Xbox360ShaderDeclaredConstantMember MakeDeclared(string name, string type, int offset, int size) =>
        Declared(name, type, offset, size);

    public static Xbox360ShaderUsedConstantMember MakeUsed(
        string name, int offset, int[] registers, string major = null, int? stride = null) =>
        Used(name, offset, registers, major, stride);

    public static Xbox360ShaderConstantBufferLayout MakeBuffer(
        int size, int bank,
        Xbox360ShaderDeclaredConstantMember[] declarations,
        Xbox360ShaderUsedConstantMember[] used) => Buffer(size, bank, declarations, used);

    public static uint U32(byte[] bytes, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset, 4));

    public static void InvokeFileSizeCheck(int size) {
        MethodInfo method = typeof(Xbox360ShaderArtifactSerializer).GetMethod("ValidateTotalFileSize", BindingFlags.Static | BindingFlags.NonPublic);
        try {
            method.Invoke(null, new object[] { size });
        } catch (TargetInvocationException exception) when (exception.InnerException != null) {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
        }
    }

    static Xbox360ShaderProgram Program(
        string name,
        string entry,
        ShaderStage stage,
        Xbox360ShaderVertexLayout profile,
        string compileIdentity,
        uint[] words,
        Xbox360ShaderInterface output) =>
        (Xbox360ShaderProgram)ProgramConstructor.Invoke(new object[] {
            $"synthetic/{name}.shader", name, entry, string.Empty, ShaderCompileTarget.Xbox360, stage,
            profile,
            "0000000000000000000000000000000000000000000000000000000000000000",
            compileIdentity,
            Convert.ToHexString(BytecodeDigest(words)), words, 1, 0,
            Array.Empty<Xbox360ShaderInterface>(), new[] { output },
            Array.Empty<Xbox360ShaderResource>(), null
        });

    static Xbox360ShaderProgram ProgramWith(
        string name, string entry, ShaderStage stage, Xbox360ShaderVertexLayout profile,
        string tool, string compile, uint[] words,
        Xbox360ShaderInterface[] inputs, Xbox360ShaderInterface[] outputs,
        Xbox360ShaderResource[] resources, Xbox360ShaderConstantBufferLayout buffer,
        string variant = "") =>
        (Xbox360ShaderProgram)ProgramConstructor.Invoke(new object[] {
            $"synthetic/{name}.shader", name, entry, variant, ShaderCompileTarget.Xbox360, stage, profile,
            tool, compile, Convert.ToHexString(BytecodeDigest(words)), words, 1, 0,
            inputs, outputs, resources, buffer
        });

    static Xbox360ShaderInterface Interface(
        Xbox360ShaderInterfaceKind kind, int index, int width, int? slot, Xbox360ShaderVertexFetchLayout fetch) =>
        (Xbox360ShaderInterface)InterfaceConstructor.Invoke(new object[] { kind, index, width, $"float{width}", slot, fetch });

    static Xbox360ShaderVertexFetchLayout Fetch(int fetch, int stride, int offset, string format) =>
        (Xbox360ShaderVertexFetchLayout)FetchConstructor.Invoke(new object[] { fetch, stride, offset, format });

    static Xbox360ShaderResource Resource(
        string name, ShaderResourceType type, int logicalSet, int logicalSlot,
        int physicalSet, int binding, int? slot, int? bank) =>
        (Xbox360ShaderResource)ResourceConstructor.Invoke(new object[] {
            name, type, logicalSet, logicalSlot, physicalSet, binding, slot, bank
        });

    static Xbox360ShaderDeclaredConstantMember Declared(string name, string type, int offset, int size) =>
        (Xbox360ShaderDeclaredConstantMember)DeclaredConstructor.Invoke(new object[] { name, type, offset, size });

    static Xbox360ShaderUsedConstantMember Used(
        string name, int offset, int[] registers, string major, int? stride) =>
        (Xbox360ShaderUsedConstantMember)UsedConstructor.Invoke(new object[] { name, offset, registers, major, stride });

    static Xbox360ShaderConstantBufferLayout Buffer(
        int size, int bank,
        Xbox360ShaderDeclaredConstantMember[] declarations,
        Xbox360ShaderUsedConstantMember[] used) =>
        (Xbox360ShaderConstantBufferLayout)BufferConstructor.Invoke(new object[] {
            size, size / 16, bank, declarations, used
        });

    static byte[] BytecodeDigest(uint[] words) {
        byte[] bytes = new byte[checked(words.Length * 4)];
        for (int index = 0; index < words.Length; index++) {
            BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(index * 4, 4), words[index]);
        }
        return SHA256.HashData(bytes);
    }

    static Xbox360ShaderInterface MakeInterface(Xbox360ShaderInterfaceKind kind, int index, int width) =>
        (Xbox360ShaderInterface)InterfaceConstructor.Invoke(new object[] { kind, index, width, "float4", null, null });

    static string FindRoot() {
        for (DirectoryInfo directory = new(AppContext.BaseDirectory); directory != null; directory = directory.Parent) {
            if (File.Exists(Path.Combine(directory.FullName, "tests", "shaders", "artifacts", "minimal-v1.x3sp"))) {
                return directory.FullName;
            }
        }
        throw new DirectoryNotFoundException("Could not locate the repository containing the X3SP golden fixture.");
    }
}
