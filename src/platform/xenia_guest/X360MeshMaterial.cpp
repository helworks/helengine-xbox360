#include "X360RenderManager3D.hpp"
#include "ShaderMaterialAsset.hpp"
#include "MaterialConstantBufferAsset.hpp"
#include "MaterialRenderState.hpp"
#include <cstring>

namespace helengine::x360 {
X360RuntimeMaterial *X360RuntimeMaterial::FromShaderAsset(::ShaderMaterialAsset *asset) {
    if (!asset || asset->Variant != HeCppString("MeshMaterialV1") || !asset->RenderState ||
        asset->RenderState->BlendMode != ::MaterialBlendMode::Opaque ||
        asset->RenderState->CullMode != ::MaterialCullMode::Back ||
        !asset->RenderState->DepthTestEnabled || !asset->RenderState->DepthWriteEnabled ||
        asset->CastsShadows || asset->ReceivesShadows ||
        !asset->DiffuseTextureAssetId.empty() || !asset->NormalTextureAssetId.empty() ||
        !asset->EmissiveTextureAssetId.empty() || !asset->RoughnessTextureAssetId.empty() ||
        !asset->ConstantBuffers || asset->ConstantBuffers->get_Length() != 1)
        he_cpp_custom::Fail("X360 MeshMaterialV1: unsupported material state");
    auto *buffer = (*asset->ConstantBuffers)[0];
    if (!buffer || buffer->Name != HeCppString("Material") || !buffer->Data || buffer->Data->get_Length() != 16)
        he_cpp_custom::Fail("X360 MeshMaterialV1: expected Material.baseColor");
    uint32_t color[4];
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *bytes = buffer->Data->Data + i * 4;
        color[i] = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
        float value;
        std::memcpy(&value, &color[i], 4);
        if (!(value >= 0.f && value <= 1.f) || (i == 3 && value != 1.f))
            he_cpp_custom::Fail("X360 MeshMaterialV1: invalid opaque color");
    }
    char path[78];
    HelShaderProgram program = {};
    if (hel_shader_mesh_path(asset->ShaderAssetId.c_str(), asset->VertexProgram.c_str(), asset->PixelProgram.c_str(), path) ||
        hel_shader_program_open(path, asset->ShaderAssetId.c_str(), asset->VertexProgram.c_str(), asset->PixelProgram.c_str(),
            asset->Variant.c_str(), 1, &program) || hel_shader_mesh_validate(&program))
        he_cpp_custom::Fail("X360 MeshMaterialV1: missing or incompatible X3SP");
    auto *material = new X360RuntimeMaterial();
    material->set_LightingModel(::RuntimeMaterialLightingModel::Unlit);
    material->program = program;
    std::memcpy(material->pixelConstants, color, sizeof(color));
    return material;
}
}
