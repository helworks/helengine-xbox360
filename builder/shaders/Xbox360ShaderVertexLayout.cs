namespace helengine.x360.builder;

/// <summary>Selects the approved native vertex-fetch contract for an Xbox 360 shader compile.</summary>
public enum Xbox360ShaderVertexLayout {
    Mesh3D,
    Batch2D
}

internal static class Xbox360ShaderVertexLayouts {
    public static string ToProtocolValue(Xbox360ShaderVertexLayout layout) => layout switch {
        Xbox360ShaderVertexLayout.Mesh3D => "mesh3d-v1",
        Xbox360ShaderVertexLayout.Batch2D => "batch2d-v1",
        _ => throw new ArgumentOutOfRangeException(nameof(layout), layout, "The Xbox 360 shader vertex layout is unsupported.")
    };

    public static Xbox360ShaderVertexLayout FromProtocolValue(string value) => value switch {
        "mesh3d-v1" => Xbox360ShaderVertexLayout.Mesh3D,
        "batch2d-v1" => Xbox360ShaderVertexLayout.Batch2D,
        _ => throw new InvalidDataException($"Compiler metadata vertex_layout '{value}' is unknown.")
    };
}
