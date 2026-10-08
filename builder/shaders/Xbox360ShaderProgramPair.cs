using helengine;

namespace helengine.x360.builder;

/// <summary>A validated vertex/pixel pairing and its bounded host upload costs.</summary>
public sealed class Xbox360ShaderProgramPair {
    public const int MaximumSafeUploadBudgetDwords = 8192;
    const int StateUploadDwordCost = 10;

    Xbox360ShaderProgramPair(
        Xbox360ShaderProgram vertex,
        Xbox360ShaderProgram pixel,
        uint programControl,
        int interpolatorCount,
        int programUploadDwords,
        int constantUploadDwords,
        int stateUploadDwords) {
        Vertex = vertex;
        Pixel = pixel;
        ProgramControl = programControl;
        InterpolatorCount = interpolatorCount;
        ProgramUploadDwords = programUploadDwords;
        ConstantUploadDwords = constantUploadDwords;
        StateUploadDwords = stateUploadDwords;
        PairUploadDwords = checked(programUploadDwords + constantUploadDwords + stateUploadDwords);
    }

    public Xbox360ShaderProgram Vertex { get; }
    public Xbox360ShaderProgram Pixel { get; }
    public uint ProgramControl { get; }
    public int InterpolatorCount { get; }
    public int ProgramUploadDwords { get; }
    public int ConstantUploadDwords { get; }
    public int StateUploadDwords { get; }
    public int PairUploadDwords { get; }

    public static Xbox360ShaderProgramPair Link(Xbox360ShaderProgram vertex, Xbox360ShaderProgram pixel) {
        ArgumentNullException.ThrowIfNull(vertex);
        ArgumentNullException.ThrowIfNull(pixel);
        if (vertex.Stage != ShaderStage.Vertex || pixel.Stage != ShaderStage.Pixel) {
            throw new InvalidDataException("Xbox360 shader pair must be linked in vertex-then-pixel stage order.");
        }
        if (vertex.Target != ShaderCompileTarget.Xbox360 || pixel.Target != ShaderCompileTarget.Xbox360) {
            throw new InvalidDataException("Xbox360 shader pair contains a program for another target.");
        }
        if (!string.Equals(vertex.Variant, pixel.Variant, StringComparison.Ordinal)) {
            throw new InvalidDataException("Xbox360 shader pair programs must use the same variant.");
        }
        if (!string.Equals(vertex.ToolIdentity, pixel.ToolIdentity, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidDataException("Xbox360 shader pair programs must have the same tool identity.");
        }

        Xbox360ShaderInterface[] vertexVaryings = vertex.Outputs
            .Where(output => output.Kind == Xbox360ShaderInterfaceKind.Location)
            .ToArray();
        foreach (Xbox360ShaderInterface input in pixel.Inputs) {
            if (input.Kind != Xbox360ShaderInterfaceKind.Location) {
                throw new InvalidDataException("Xbox360 pixel inputs must be location varyings.");
            }
            Xbox360ShaderInterface output = vertexVaryings.SingleOrDefault(candidate => candidate.Index == input.Index);
            if (output == null) {
                throw new InvalidDataException($"Xbox360 pixel input LOCATION{input.Index} has no vertex output.");
            }
            if (output.Width != input.Width) {
                throw new InvalidDataException($"Xbox360 varying LOCATION{input.Index} width differs between vertex and pixel programs.");
            }
            if (!output.PhysicalSlot.HasValue || !input.PhysicalSlot.HasValue
                || output.PhysicalSlot.Value != input.PhysicalSlot.Value) {
                throw new InvalidDataException($"Xbox360 varying LOCATION{input.Index} has mismatched physical slots.");
            }
        }

        int highestVaryingSlot = vertexVaryings.Length == 0
            ? 0
            : vertexVaryings.Max(output => output.PhysicalSlot.Value);
        int interpolatorCount = vertexVaryings.Length == 0 ? 0 : checked(highestVaryingSlot + 1);
        uint programControl = (uint)vertex.RegisterField
            | ((uint)pixel.RegisterField << 8)
            | ((uint)highestVaryingSlot << 20);

        int programUploadDwords = checked(vertex.WordCount + pixel.WordCount + 6);
        int constantUploadDwords = checked(ConstantCost(vertex) + ConstantCost(pixel));
        return new Xbox360ShaderProgramPair(
            vertex,
            pixel,
            programControl,
            interpolatorCount,
            programUploadDwords,
            constantUploadDwords,
            StateUploadDwordCost);
    }

    public void ValidateUploadBudget(int availableDwords) {
        if (availableDwords <= 0 || availableDwords > MaximumSafeUploadBudgetDwords) {
            throw new ArgumentOutOfRangeException(nameof(availableDwords),
                $"Available upload budget must be between 1 and {MaximumSafeUploadBudgetDwords} dwords.");
        }
        if (availableDwords < PairUploadDwords) {
            throw new InvalidOperationException(
                $"Shader pair requires {PairUploadDwords} upload dwords; only {availableDwords} are available.");
        }
    }

    static int ConstantCost(Xbox360ShaderProgram program) {
        int capacity = program.ConstantBuffer?.RegisterCapacity ?? 0;
        return checked(capacity * 4 + (capacity > 0 ? 2 : 0));
    }
}
