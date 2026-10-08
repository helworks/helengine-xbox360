/*
 * The engine's 3D renderer for this console.
 *
 * RenderManager3D::Draw is an empty virtual in the engine: the platform owns
 * the traversal. This walks the same path the Dreamcast port walks. For each
 * camera the object manager holds, it reads the camera's render queue and
 * visits every drawable in order; for each drawable it takes the owning
 * entity's world transform, multiplies it by the camera's view and
 * projection, and hands the mesh and that matrix to the Xenos backend
 * (mesh.c).
 *
 * Shading is the cooked material's base colour under one directional light
 * plus ambient, both read from the scene every frame so a light switched off
 * while running takes effect. The lighting math runs in the vertex shader
 * from the normal carried in the vertex stream, rather than being baked into
 * vertex colours at upload, for the same reason.
 *
 * What is still missing is named where it happens: textures, more than one
 * directional light, and per-submesh materials.
 */
#include "X360RenderManager3D.hpp"

#include "X360RenderManager2D.hpp"

#include "Entity.hpp"
#include "ICamera.hpp"
#include "IDrawable3D.hpp"
#include "IRenderQueue3D.hpp"
#include "IRenderVisitor3D.hpp"
#include "CameraClearSettings.hpp"
#include "CameraProjectionUtils.hpp"
#include "ModelAsset.hpp"
#include "ObjectManager.hpp"
#include "AmbientLightComponent.hpp"
#include "AssetSerializer.hpp"
#include "DirectionalLightComponent.hpp"
#include "LightDirectionUtility.hpp"
#include "IContentStreamSource.hpp"
#include "PlatformMaterialAsset.hpp"
#include "EngineBinaryHeader.hpp"
#include "EngineBinaryHeaderSerializer.hpp"
#include "ShaderMaterialAsset.hpp"
#include "ShaderMaterialAssetBinarySerializer.hpp"
#include "X360EngineMath.hpp"
#include "float2.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "float4x4.hpp"

extern "C" {
#include "../../engine/math.h"
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
}

namespace {

/* A cooked model carries no vertex colours, so every vertex is white and the
   visible colour comes from the material and the lighting. This used to be a
   colour derived from the normal, which made a wrong normal or winding
   obvious; the normal now reaches the GPU itself, so lighting shows the same
   faults without standing in for the material. */
const float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};

} // namespace

namespace helengine::x360 {

/// <summary>Collects the drawables a camera's queue visits and draws each one.</summary>
class DrawableVisitor final : public ::IRenderVisitor3D {
public:
    DrawableVisitor(const HelMat4 &viewProjection, uint16_t cameraLayerMask,
                    const HelDrawParams &lighting, X360RenderManager3D &owner)
        : viewProjection_(viewProjection), cameraLayerMask_(cameraLayerMask),
          lighting_(lighting), owner_(owner) {}

    void Visit(::IDrawable3D *drawable) override {
        if (drawable == nullptr) {
            skipped_++;
            return;
        }
        ::Entity *entity = drawable->get_Parent();
        if (entity == nullptr || !entity->get_IsHierarchyEnabled() ||
            (entity->get_LayerMask() & cameraLayerMask_) == 0) {
            skipped_++;
            return;
        }
        X360RuntimeModel *model =
            dynamic_cast<X360RuntimeModel *>(drawable->get_Model());
        if (model == nullptr || model->mesh() == 0) {
            skipped_++;
            return;
        }

        /* Column vectors in the backend, so view-projection comes first and
           the model transform last. */
        ::float4x4 world = entity->get_WorldTransformMatrix();
        HelMat4 modelMatrix = hel_matrix_from_engine(world);
        HelMat4 mvp = hel_mul(viewProjection_, modelMatrix);

        /* The scene's lighting, with this drawable's own material colour on
           top. A drawable with no material draws white, which is what an
           untextured, uncoloured surface is. */
        HelDrawParams params = lighting_;
        const X360RuntimeMaterial *material = firstMaterial(drawable);
        if (material != nullptr) {
            params.base_color[0] = (float)((material->baseColor >> 24) & 0xFFu) / 255.0f;
            params.base_color[1] = (float)((material->baseColor >> 16) & 0xFFu) / 255.0f;
            params.base_color[2] = (float)((material->baseColor >> 8) & 0xFFu) / 255.0f;
            params.base_color[3] = (float)(material->baseColor & 0xFFu) / 255.0f;
        }

        // Column-major storage of the backend transpose is the Engine row matrix.
        const int error = material && material->program.view.data
            ? hel_draw_mesh_program(model->mesh(), &material->program, mvp.m, material->pixelConstants)
            : hel_draw_shaded(model->mesh(), &mvp, &modelMatrix, &params);
        if (error != HEL_OK) {
            owner_.recordDrawError(error);
            return;
        }
        drawn_++;
    }

    uint32_t drawn() const { return drawn_; }
    uint32_t skipped() const { return skipped_; }

private:
    /// <summary>The drawable's first material, or null when it has none.</summary>
    static const X360RuntimeMaterial *firstMaterial(::IDrawable3D *drawable) {
        Array<::RuntimeMaterial *> *materials = drawable->get_Materials();
        if (materials == nullptr || materials->get_Length() == 0) {
            return nullptr;
        }
        /* Submesh materials are not drawn separately yet: the backend issues
           one draw for the whole mesh, so only the first material can apply.
           A mesh with several would need one draw per submesh. */
        return dynamic_cast<const X360RuntimeMaterial *>((*materials)[0]);
    }

    const HelMat4 &viewProjection_;
    uint16_t cameraLayerMask_;
    const HelDrawParams &lighting_;
    X360RenderManager3D &owner_;
    uint32_t drawn_ = 0;
    uint32_t skipped_ = 0;
};

X360RuntimeModel::X360RuntimeModel(HelMesh mesh, uint32_t vertexCount,
                                   uint32_t indexCount)
    : mesh_(mesh), vertexCount_(vertexCount), indexCount_(indexCount) {}

X360RuntimeModel::~X360RuntimeModel() { Dispose(); }

void X360RuntimeModel::Dispose() {
    if (mesh_ != 0) {
        hel_mesh_release(mesh_);
        mesh_ = 0;
    }
    ::RuntimeModel::Dispose();
}

::RuntimeModel *X360RenderManager3D::BuildModelFromRaw(::ModelAsset *data) {
    if (data == nullptr || data->Positions == nullptr) {
        hel_log("HEL render3d: model rejected, no positions");
        return nullptr;
    }
    if (data->Indices16 == nullptr) {
        /* 32-bit indices would need a second index path in the backend; the
           engine's own cooked models use 16-bit indices. */
        hel_log("HEL render3d: model rejected, 16-bit indices required");
        return nullptr;
    }

    const int32_t vertexCount = data->Positions->get_Length();
    const int32_t indexCount = data->Indices16->get_Length();
    if (vertexCount <= 0 || indexCount <= 0) {
        hel_log("HEL render3d: model rejected, empty");
        return nullptr;
    }
    const bool hasNormals = data->Normals != nullptr &&
                            data->Normals->get_Length() == vertexCount;
    const bool hasTexCoords = data->TexCoords != nullptr &&
                              data->TexCoords->get_Length() == vertexCount;

    HelVertex *vertices = new HelVertex[vertexCount];
    for (int32_t index = 0; index < vertexCount; ++index) {
        const ::float3 &position = (*data->Positions)[index];
        vertices[index].x = position.X;
        vertices[index].y = position.Y;
        vertices[index].z = position.Z;
        /* A model without normals is drawn facing the viewer rather than
           refused, because its geometry is still correct; it just cannot be
           lit. The count is reported once below. */
        if (hasNormals) {
            const ::float3 &normal = (*data->Normals)[index];
            vertices[index].nx = normal.X;
            vertices[index].ny = normal.Y;
            vertices[index].nz = normal.Z;
        } else {
            vertices[index].nx = 0.0f;
            vertices[index].ny = 0.0f;
            vertices[index].nz = 1.0f;
        }
        /* Texture coordinates travel with every vertex now. Scene materials
           are untextured, so a cooked model's own coordinates are carried
           when it has them and zero otherwise; nothing samples them yet. */
        if (hasTexCoords) {
            const ::float2 &uv = (*data->TexCoords)[index];
            vertices[index].u = uv.X;
            vertices[index].v = uv.Y;
        } else {
            vertices[index].u = 0.0f;
            vertices[index].v = 0.0f;
        }
        vertices[index].r = kWhite[0];
        vertices[index].g = kWhite[1];
        vertices[index].b = kWhite[2];
        vertices[index].a = kWhite[3];
    }

    uint16_t *indices = new uint16_t[indexCount];
    for (int32_t index = 0; index < indexCount; ++index) {
        indices[index] = (*data->Indices16)[index];
    }

    HelMesh mesh = 0;
    const int error = hel_mesh_create(vertices, (uint32_t)vertexCount, indices,
                                      (uint32_t)indexCount, &mesh);
    delete[] indices;
    delete[] vertices;
    if (error != HEL_OK) {
        hel_log_u32("HEL render3d: mesh upload failed with", (uint32_t)error);
        return nullptr;
    }

    models_++;
    if (!hasNormals) {
        hel_log("HEL render3d: model has no normals, it cannot be lit");
    }
    hel_log_u32("HEL render3d: model built, vertices", (uint32_t)vertexCount);
    hel_log_u32("HEL render3d: model built, indices", (uint32_t)indexCount);
    X360RuntimeModel *runtimeModel = new X360RuntimeModel(
        mesh, (uint32_t)vertexCount, (uint32_t)indexCount);
    runtimeModel->SetRawModelAsset(data);
    runtimeModel->SetBounds(data->BoundsMin, data->BoundsMax);
    return runtimeModel;
}

::RuntimeModel *X360RenderManager3D::BuildModelFromCooked(
    HeCppString cookedAssetPath, ::IContentStreamSource *contentStreamSource) {
    /* A cooked model is a serialized ModelAsset in the archive. Once read it is
       the same thing BuildModelFromRaw already uploads, so the two paths do not
       differ after this point. */
    if (!contentStreamSource) {
        hel_log("HEL render3d: cooked model requested with no content source");
        return nullptr;
    }
    ::Stream *stream = contentStreamSource->OpenRead(cookedAssetPath);
    if (!stream) {
        hel_log("HEL render3d: cooked model is not in the image");
        hel_log(cookedAssetPath.c_str());
        return nullptr;
    }
    ::Asset *deserialized = ::AssetSerializer::Deserialize(stream);
    stream->Dispose();
    delete stream;
    ::ModelAsset *asset = dynamic_cast<::ModelAsset *>(deserialized);
    if (!asset) {
        hel_log("HEL render3d: cooked model did not deserialize");
        hel_log(cookedAssetPath.c_str());
        delete deserialized;
        return nullptr;
    }
    ::RuntimeModel *model = BuildModelFromRaw(asset);
    // Successful upload transfers the raw asset to RuntimeModel. Failed upload
    // has no owner to release its arrays, so this cooked path retains that duty.
    if (model == nullptr) { asset->Dispose(); delete asset; }
    return model;
}

#if defined(HELENGINE_RUNTIME_MATERIAL_RESOLUTION_COOKED_PLATFORM_OWNED)
::RuntimeMaterial *
X360RenderManager3D::BuildMaterialFromCooked(::PlatformMaterialAsset *materialAsset) {
    X360RuntimeMaterial *material = new X360RuntimeMaterial();
    /* Unlit is not a preference here, it is what the backend does: the pixel
       shader has no lighting. The cooker rejects any other lighting mode, so a
       cooked material always agrees with this. */
    material->set_LightingModel(::RuntimeMaterialLightingModel::Unlit);
    if (materialAsset) {
        material->baseColor = ((uint32_t)materialAsset->BaseColorR << 24) |
                              ((uint32_t)materialAsset->BaseColorG << 16) |
                              ((uint32_t)materialAsset->BaseColorB << 8) |
                              (uint32_t)materialAsset->BaseColorA;
    }
    if (materials_ == 0) {
        hel_log_u32("HEL render3d: first cooked material colour RGBA",
                    material->baseColor);
    }
    materials_++;
    return material;
}

::RuntimeMaterial *X360RenderManager3D::BuildMaterialFromCooked(
    HeCppString cookedAssetPath, ::IContentStreamSource *contentStreamSource) {
    if (!contentStreamSource) {
        hel_log("HEL render3d: cooked material requested with no content source");
        return BuildMaterialFromCooked((::PlatformMaterialAsset *)nullptr);
    }
    ::Stream *stream = contentStreamSource->OpenRead(cookedAssetPath);
    if (!stream) {
        hel_log("HEL render3d: cooked material is not in the image");
        hel_log(cookedAssetPath.c_str());
        return BuildMaterialFromCooked((::PlatformMaterialAsset *)nullptr);
    }
    auto *header = ::EngineBinaryHeaderSerializer::Read(stream);
    if (header->FormatId == ::ShaderMaterialAssetBinarySerializer::FormatId) {
        auto *asset = ::ShaderMaterialAssetBinarySerializer::Deserialize(stream, header);
        auto *material = X360RuntimeMaterial::FromShaderAsset(asset);
        asset->Dispose();
        delete asset;
        stream->Dispose();
        delete stream;
        materials_++;
        return material;
    }
    delete header;
    stream->SetPosition(0);
    /* The payload is whatever the cooker serialized. A different asset type
       here means the build staged the wrong file, which is worth saying rather
       than reading as a default-coloured material. */
    ::PlatformMaterialAsset *asset = dynamic_cast<::PlatformMaterialAsset *>(
        ::AssetSerializer::Deserialize(stream));
    if (!asset) {
        hel_log("HEL render3d: cooked material did not deserialize");
        hel_log(cookedAssetPath.c_str());
    }
    auto *material = BuildMaterialFromCooked(asset);
    // PlatformMaterialAsset has a destructor, but no Dispose in the generated ABI.
    delete asset;
    stream->Dispose();
    delete stream;
    return material;
}
#endif

/* The background the scene asked for.

   A scene has several cameras and each carries its own clear settings, so the
   one that owns the background is the first to draw: the lowest draw order
   that actually clears colour. The overlay cameras above it leave the colour
   buffer alone, which is how they compose over it.

   Depth is not read from here. The backend clears depth every frame because
   its depth buffer is EDRAM that does not survive the resolve, so a camera
   asking not to clear depth could not be honoured anyway. */
bool X360RenderManager3D::resolveClearColor(float *rgba) {
    ::Core *core = get_OwnerCore();
    ::ObjectManager *objectManager =
        core != nullptr ? core->get_ObjectManager() : nullptr;
    List<::ICamera *> *cameras =
        objectManager != nullptr ? objectManager->get_Cameras() : nullptr;
    const int32_t cameraCount = cameras != nullptr ? cameras->get_Count() : 0;

    ::ICamera *chosen = nullptr;
    for (int32_t index = 0; index < cameraCount; ++index) {
        ::ICamera *camera = cameras->get_Item(index);
        if (camera == nullptr) {
            continue;
        }
        ::CameraClearSettings settings = camera->get_ClearSettings();
        if (!settings.get_ClearColorEnabled()) {
            continue;
        }
        if (chosen == nullptr ||
            camera->get_CameraDrawOrder() < chosen->get_CameraDrawOrder()) {
            chosen = camera;
        }
    }
    if (chosen == nullptr || rgba == nullptr) {
        return false;
    }
    ::CameraClearSettings settings = chosen->get_ClearSettings();
    const ::float4 colour = settings.get_ClearColor();
    if (!reportedClearColor_) {
        reportedClearColor_ = true;
        /* Reported as 8-bit channels so the log line can be compared with a
           pixel read straight from a capture. */
        hel_log_u32("HEL render3d: authored clear colour RGBA",
                    ((uint32_t)(colour.X * 255.0f + 0.5f) << 24) |
                        ((uint32_t)(colour.Y * 255.0f + 0.5f) << 16) |
                        ((uint32_t)(colour.Z * 255.0f + 0.5f) << 8) |
                        (uint32_t)(colour.W * 255.0f + 0.5f));
    }
    rgba[0] = colour.X;
    rgba[1] = colour.Y;
    rgba[2] = colour.Z;
    rgba[3] = colour.W;
    return true;
}

/* Resolves the scene's lights into the constants one draw shades with.
   Read fresh every frame, because a scene can switch a light off while
   running and the cube scene does exactly that.

   Only the first directional light is used: the shader has one light term.
   Ambient lights are summed, which is what adding more of them means. No
   light is invented when a scene has none; the result is then black, and the
   count in the log says why rather than a made-up value hiding it. */
HelDrawParams X360RenderManager3D::resolveLighting(::ObjectManager *objectManager) {
    HelDrawParams params = {};
    params.base_color[0] = 1.0f;
    params.base_color[1] = 1.0f;
    params.base_color[2] = 1.0f;
    params.base_color[3] = 1.0f;
    if (objectManager == nullptr) {
        return params;
    }

    List<::AmbientLightComponent *> *ambientLights =
        objectManager->get_AmbientLights();
    const int32_t ambientCount =
        ambientLights != nullptr ? ambientLights->get_Count() : 0;
    for (int32_t index = 0; index < ambientCount; ++index) {
        ::AmbientLightComponent *light = ambientLights->get_Item(index);
        if (light == nullptr) {
            continue;
        }
        const ::float4 colour = light->get_Color();
        const float intensity = light->get_Intensity();
        params.ambient[0] += colour.X * intensity;
        params.ambient[1] += colour.Y * intensity;
        params.ambient[2] += colour.Z * intensity;
    }

    List<::DirectionalLightComponent *> *directionalLights =
        objectManager->get_DirectionalLights();
    const int32_t directionalCount =
        directionalLights != nullptr ? directionalLights->get_Count() : 0;
    for (int32_t index = 0; index < directionalCount; ++index) {
        ::DirectionalLightComponent *light = directionalLights->get_Item(index);
        if (light == nullptr || light->get_Parent() == nullptr) {
            continue;
        }
        /* The engine derives a directional light's world direction from its
           entity's forward axis; use its own utility rather than repeating
           that convention here, where it could drift. The utility returns the
           direction the light travels, and shading wants the vector from the
           surface towards it, which is the opposite. */
        const ::float3 travel = ::LightDirectionUtility::GetLightDirection(light);
        params.light_direction[0] = -travel.X;
        params.light_direction[1] = -travel.Y;
        params.light_direction[2] = -travel.Z;
        const ::float4 colour = light->get_Color();
        const float intensity = light->get_Intensity();
        params.light_color[0] = colour.X * intensity;
        params.light_color[1] = colour.Y * intensity;
        params.light_color[2] = colour.Z * intensity;
        break;
    }

    if (!reportedFirstDraw_) {
        hel_log_u32("HEL render3d: ambient lights", (uint32_t)ambientCount);
        hel_log_u32("HEL render3d: directional lights",
                    (uint32_t)directionalCount);
    }
    return params;
}

/* One camera's 3D content. Returns without drawing when the camera cannot
   describe a view, which is not an error: a camera that carries only an
   interface is exactly that. */
void X360RenderManager3D::drawCamera3D(::ICamera *camera,
                                       const HelDrawParams &lighting,
                                       float aspect) {
    ::Entity *cameraEntity = camera->get_Parent();
    if (cameraEntity == nullptr) {
        return;
    }

    /* The view matrix is the inverse of the camera's world transform. The
       generated matrix type exposes the inverse transpose rather than the
       inverse, so transposing that gives the inverse. */
    ::float4x4 worldFromCamera = cameraEntity->get_WorldTransformMatrix();
    ::float4x4 inverseTranspose;
    if (!::float4x4::TryInverseTranspose__ref0_out1(worldFromCamera,
                                                    inverseTranspose)) {
        hel_log("HEL render3d: camera transform is not invertible");
        return;
    }
    ::float4x4 view;
    ::float4x4::Transpose__ref0_out1(inverseTranspose, view);
    /* The field of view comes from the authored camera, so a scene frames the
       same way here as on any other platform. */
    ::float4x4 projection =
        ::CameraProjectionUtils::CreatePerspectiveProjection(camera, aspect);

    /* The backend composes with column vectors, so projection comes first;
       the conversion transposes each engine matrix. */
    HelMat4 viewProjection = hel_mul(hel_matrix_from_engine(projection),
                                     hel_matrix_from_engine(view));

    ::IRenderQueue3D *queue = camera->get_RenderQueue3D();
    if (queue == nullptr) {
        return;
    }
    DrawableVisitor visitor(viewProjection, camera->get_LayerMask(), lighting,
                            *this);
    queue->VisitOrdered(&visitor);
    lastDrawnCount_ += visitor.drawn();
    lastSkippedCount_ += visitor.skipped();
}

void X360RenderManager3D::Draw() {
    lastDrawnCount_ = 0;
    lastSkippedCount_ = 0;
    lastDrawError_ = HEL_OK;

    ::Core *core = get_OwnerCore();
    ::ObjectManager *objectManager =
        core != nullptr ? core->get_ObjectManager() : nullptr;
    List<::ICamera *> *cameras =
        objectManager != nullptr ? objectManager->get_Cameras() : nullptr;
    const int32_t cameraCount = cameras != nullptr ? cameras->get_Count() : 0;
    if (cameraCount == 0) {
        /* An empty scene draws nothing; that is not an error. */
        return;
    }

    const HelDrawParams lighting = resolveLighting(objectManager);

    const float width = (float)hel_video_width();
    const float height = (float)hel_video_height();
    const float aspect = height > 0.0f ? width / height : 1.0f;

    /* The engine's frame calls only reach the 3D manager, so the 2D pass is
       driven from here, per camera, right after that camera's 3D content. The
       Windows backend does the same. Without it an authored interface reaches
       the console and is never drawn. */
    X360RenderManager2D *render2d = dynamic_cast<X360RenderManager2D *>(
        core != nullptr ? core->get_RenderManager2D() : nullptr);
    if (render2d != nullptr) {
        render2d->beginFrame();
    }

    /* Draw order decides what covers what, and the camera list is not in that
       order, so each pass takes the lowest order not yet drawn. The counts are
       small enough that this costs nothing worth avoiding. */
    int32_t visited = 0;
    int32_t previousOrder = -1;
    while (visited < cameraCount) {
        int32_t nextOrder = -1;
        for (int32_t index = 0; index < cameraCount; ++index) {
            ::ICamera *candidate = cameras->get_Item(index);
            if (candidate == nullptr) {
                visited++;
                continue;
            }
            const int32_t order = (int32_t)candidate->get_CameraDrawOrder();
            if (order <= previousOrder) {
                continue;
            }
            if (nextOrder < 0 || order < nextOrder) {
                nextOrder = order;
            }
        }
        if (nextOrder < 0) {
            break;
        }
        previousOrder = nextOrder;
        for (int32_t index = 0; index < cameraCount; ++index) {
        ::ICamera *camera = cameras->get_Item(index);
        if (camera == nullptr) {
            continue;
        }
        if ((int32_t)camera->get_CameraDrawOrder() != nextOrder) {
            continue;
        }
        visited++;
        drawCamera3D(camera, lighting, aspect);
        /* Always, even when the 3D pass above returned early. A camera that
           carries only an interface has no 3D queue and no transform worth
           inverting, and skipping its 2D content because of that is exactly
           how an authored interface goes missing. */
        if (render2d != nullptr) {
            render2d->renderCamera(camera);
        }
        }
    }

    if (render2d != nullptr) {
        render2d->endFrame();
    }

    if (!reportedFirstDraw_) {
        reportedFirstDraw_ = true;
        hel_log_u32("HEL render3d: first draw, cameras", (uint32_t)cameraCount);
        hel_log_u32("HEL render3d: first draw, drawables drawn",
                    lastDrawnCount_);
        hel_log_u32("HEL render3d: first draw, drawables skipped",
                    lastSkippedCount_);
    }
}

int32_t X360RenderManager3D::get_LastDrawCallCount() {
    return (int32_t)lastDrawnCount_;
}

void X360RenderManager3D::recordDrawError(int error) {
    if (lastDrawError_ == HEL_OK) {
        lastDrawError_ = error;
        hel_log_u32("HEL render3d: backend draw failed with", (uint32_t)error);
    }
}

void X360RenderManager3D::ReleaseModel(::RuntimeModel *model) {
    // Core invokes the model's virtual Dispose before deleting it. The platform
    // override retires GPU storage; present releases it after its final use.
    ::RenderManager3D::ReleaseModel(model);
}

} // namespace helengine::x360
