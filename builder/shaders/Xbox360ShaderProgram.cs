using helengine;

namespace helengine.x360.builder;

public enum Xbox360ShaderInterfaceKind {
    Location,
    Position
}

public sealed class Xbox360ShaderVertexFetchLayout {
    internal Xbox360ShaderVertexFetchLayout(int fetchConstant, int stride, int offset, string hardwareFormat) {
        FetchConstant = fetchConstant;
        Stride = stride;
        Offset = offset;
        HardwareFormat = hardwareFormat;
    }

    public int FetchConstant { get; }
    public int Stride { get; }
    public int Offset { get; }
    public string HardwareFormat { get; }
}

public sealed class Xbox360ShaderInterface {
    internal Xbox360ShaderInterface(
        Xbox360ShaderInterfaceKind kind,
        int index,
        int width,
        string format,
        int? physicalSlot,
        Xbox360ShaderVertexFetchLayout vertexFetch) {
        Kind = kind;
        Index = index;
        Width = width;
        Format = format;
        PhysicalSlot = physicalSlot;
        VertexFetch = vertexFetch;
    }

    public Xbox360ShaderInterfaceKind Kind { get; }
    public int Index { get; }
    public int Width { get; }
    public string Format { get; }
    public int? PhysicalSlot { get; }
    public Xbox360ShaderVertexFetchLayout VertexFetch { get; }
}

public sealed class Xbox360ShaderDeclaredConstantMember {
    internal Xbox360ShaderDeclaredConstantMember(string name, string type, int offsetBytes, int sizeBytes) {
        Name = name;
        Type = type;
        OffsetBytes = offsetBytes;
        SizeBytes = sizeBytes;
    }

    public string Name { get; }
    public string Type { get; }
    public int OffsetBytes { get; }
    public int SizeBytes { get; }
}

public sealed class Xbox360ShaderUsedConstantMember {
    internal Xbox360ShaderUsedConstantMember(
        string name,
        int offsetBytes,
        IEnumerable<int> registerIndices,
        string matrixMajor,
        int? matrixStrideBytes) {
        Name = name;
        OffsetBytes = offsetBytes;
        RegisterIndices = Array.AsReadOnly(registerIndices.ToArray());
        MatrixMajor = matrixMajor;
        MatrixStrideBytes = matrixStrideBytes;
    }

    public string Name { get; }
    public int OffsetBytes { get; }
    public IReadOnlyList<int> RegisterIndices { get; }
    public string MatrixMajor { get; }
    public int? MatrixStrideBytes { get; }
}

public sealed class Xbox360ShaderConstantBufferLayout {
    internal Xbox360ShaderConstantBufferLayout(
        int sizeBytes,
        int registerCapacity,
        int physicalBankBaseRegister,
        IEnumerable<Xbox360ShaderDeclaredConstantMember> declaredMembers,
        IEnumerable<Xbox360ShaderUsedConstantMember> usedMembers) {
        SizeBytes = sizeBytes;
        RegisterCapacity = registerCapacity;
        PhysicalBankBaseRegister = physicalBankBaseRegister;
        DeclaredMembers = Array.AsReadOnly(declaredMembers.ToArray());
        UsedMembers = Array.AsReadOnly(usedMembers.ToArray());
    }

    public int SizeBytes { get; }
    public int RegisterCapacity { get; }
    public int PhysicalBankBaseRegister { get; }
    public IReadOnlyList<Xbox360ShaderDeclaredConstantMember> DeclaredMembers { get; }
    public IReadOnlyList<Xbox360ShaderUsedConstantMember> UsedMembers { get; }
}

public sealed class Xbox360ShaderResource {
    internal Xbox360ShaderResource(
        string name,
        ShaderResourceType type,
        int logicalSet,
        int logicalSlot,
        int physicalSet,
        int physicalBinding,
        int? physicalSlot,
        int? physicalBankBaseRegister) {
        Name = name;
        Type = type;
        LogicalSet = logicalSet;
        LogicalSlot = logicalSlot;
        PhysicalSet = physicalSet;
        PhysicalBinding = physicalBinding;
        PhysicalSlot = physicalSlot;
        PhysicalBankBaseRegister = physicalBankBaseRegister;
    }

    public string Name { get; }
    public ShaderResourceType Type { get; }
    public int LogicalSet { get; }
    public int LogicalSlot { get; }
    public int PhysicalSet { get; }
    public int PhysicalBinding { get; }
    public int? PhysicalSlot { get; }
    public int? PhysicalBankBaseRegister { get; }
}

/// <summary>A validated, immutable host-side snapshot of one compiled Xenos shader stage.</summary>
public sealed class Xbox360ShaderProgram {
    internal Xbox360ShaderProgram(
        string sourcePath,
        string programName,
        string entryPoint,
        string variant,
        ShaderCompileTarget target,
        ShaderStage stage,
        Xbox360ShaderVertexLayout vertexLayout,
        string toolIdentity,
        string compileIdentity,
        string bytecodeSha256,
        IEnumerable<uint> microcodeWords,
        int registerCount,
        int registerField,
        IEnumerable<Xbox360ShaderInterface> inputs,
        IEnumerable<Xbox360ShaderInterface> outputs,
        IEnumerable<Xbox360ShaderResource> resources,
        Xbox360ShaderConstantBufferLayout constantBuffer) {
        SourcePath = sourcePath;
        ProgramName = programName;
        EntryPoint = entryPoint;
        Variant = variant;
        Target = target;
        Stage = stage;
        VertexLayout = vertexLayout;
        ToolIdentity = toolIdentity;
        CompileIdentity = compileIdentity;
        BytecodeSha256 = bytecodeSha256;
        MicrocodeWords = Array.AsReadOnly(microcodeWords.ToArray());
        RegisterCount = registerCount;
        RegisterField = registerField;
        Inputs = Array.AsReadOnly(inputs.ToArray());
        Outputs = Array.AsReadOnly(outputs.ToArray());
        Resources = Array.AsReadOnly(resources.ToArray());
        ConstantBuffer = constantBuffer;
    }

    public string SourcePath { get; }
    public string ProgramName { get; }
    public string EntryPoint { get; }
    public string Variant { get; }
    public ShaderCompileTarget Target { get; }
    public ShaderStage Stage { get; }
    public Xbox360ShaderVertexLayout VertexLayout { get; }
    public string ToolIdentity { get; }
    public string CompileIdentity { get; }
    public string BytecodeSha256 { get; }
    public IReadOnlyList<uint> MicrocodeWords { get; }
    public int WordCount => MicrocodeWords.Count;
    public int RegisterCount { get; }
    public int RegisterField { get; }
    public IReadOnlyList<Xbox360ShaderInterface> Inputs { get; }
    public IReadOnlyList<Xbox360ShaderInterface> Outputs { get; }
    public IReadOnlyList<Xbox360ShaderResource> Resources { get; }
    public Xbox360ShaderConstantBufferLayout ConstantBuffer { get; }
}
