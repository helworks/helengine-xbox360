using helengine;

namespace helengine.x360.builder;

/// <summary>Explicit, textureless MeshMaterialV1 ABI. Logical cbuffer slots are unrestricted.</summary>
public static class Xbox360MeshMaterialContract {
    public static void Validate(Xbox360ShaderProgramPair pair) {
        ValidateStage(pair.Vertex, "Transform", "mvp", "float4x4", 64, 0);
        ValidateStage(pair.Pixel, "Material", "baseColor", "float4", 16, 256);
    }

    static void ValidateStage(Xbox360ShaderProgram stage, string buffer, string member, string type, int bytes, int bank) {
        var layout = stage.ConstantBuffer;
        if (stage.VertexLayout != Xbox360ShaderVertexLayout.Mesh3D || stage.Resources.Count != 1 ||
            stage.Resources[0].Name != buffer || stage.Resources[0].Type != ShaderResourceType.ConstantBuffer ||
            layout == null || layout.SizeBytes != bytes || layout.RegisterCapacity != bytes / 16 ||
            layout.PhysicalBankBaseRegister != bank || layout.DeclaredMembers.Count != 1 || layout.UsedMembers.Count != 1)
            throw new InvalidOperationException("X360_MESH_MATERIAL_ABI: expected Transform.mvp (VS float4x4) and Material.baseColor (PS float4), without textures.");
        var declared = layout.DeclaredMembers[0];
        var used = layout.UsedMembers[0];
        if (declared.Name != member || declared.Type != type || declared.OffsetBytes != 0 || declared.SizeBytes != bytes ||
            used.Name != member || used.OffsetBytes != 0 || !used.RegisterIndices.SequenceEqual(Enumerable.Range(0, bytes / 16)))
            throw new InvalidOperationException("X360_MESH_MATERIAL_ABI: unexpected member layout or usage.");
    }
}
