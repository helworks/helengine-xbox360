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
 * One deliberate simplification, visible in the log and with a clear
 * replacement:
 *
 *   Colour. A model carries positions, normals and texture coordinates but
 *   no vertex colours, and materials are not wired up yet, so each vertex
 *   takes a colour derived from its normal. That makes the faces of a mesh
 *   distinguishable and makes a wrong normal or a wrong winding obvious.
 *   When materials arrive, the colour comes from the material instead.
 */
#include "X360RenderManager3D.hpp"

#include "Entity.hpp"
#include "ICamera.hpp"
#include "IDrawable3D.hpp"
#include "IRenderQueue3D.hpp"
#include "IRenderVisitor3D.hpp"
#include "CameraProjectionUtils.hpp"
#include "ModelAsset.hpp"
#include "ObjectManager.hpp"
#include "X360EngineMath.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "float4x4.hpp"

extern "C" {
#include "../../engine/math.h"
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
}

namespace {

/* Maps a unit normal to a colour, so each face of a mesh is visibly
   different and a flipped normal shows up as a colour change. */
void colour_from_normal(const ::float3 &normal, float *rgba) {
    rgba[0] = 0.5f + 0.5f * normal.X;
    rgba[1] = 0.5f + 0.5f * normal.Y;
    rgba[2] = 0.5f + 0.5f * normal.Z;
    rgba[3] = 1.0f;
}

} // namespace

namespace helengine::x360 {

/// <summary>Collects the drawables a camera's queue visits and draws each one.</summary>
class DrawableVisitor final : public ::IRenderVisitor3D {
public:
    DrawableVisitor(const HelMat4 &viewProjection, uint16_t cameraLayerMask,
                    X360RenderManager3D &owner)
        : viewProjection_(viewProjection), cameraLayerMask_(cameraLayerMask),
          owner_(owner) {}

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
        HelMat4 mvp = hel_mul(viewProjection_, hel_matrix_from_engine(world));

        const int error = hel_draw_unlit(model->mesh(), &mvp);
        if (error != HEL_OK) {
            owner_.recordDrawError(error);
            return;
        }
        drawn_++;
    }

    uint32_t drawn() const { return drawn_; }
    uint32_t skipped() const { return skipped_; }

private:
    const HelMat4 &viewProjection_;
    uint16_t cameraLayerMask_;
    X360RenderManager3D &owner_;
    uint32_t drawn_ = 0;
    uint32_t skipped_ = 0;
};

X360RuntimeModel::X360RuntimeModel(HelMesh mesh, uint32_t vertexCount,
                                   uint32_t indexCount)
    : mesh_(mesh), vertexCount_(vertexCount), indexCount_(indexCount) {}

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

    HelVertex *vertices = new HelVertex[vertexCount];
    for (int32_t index = 0; index < vertexCount; ++index) {
        const ::float3 &position = (*data->Positions)[index];
        vertices[index].x = position.X;
        vertices[index].y = position.Y;
        vertices[index].z = position.Z;
        float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        if (hasNormals) {
            colour_from_normal((*data->Normals)[index], rgba);
        }
        vertices[index].r = rgba[0];
        vertices[index].g = rgba[1];
        vertices[index].b = rgba[2];
        vertices[index].a = rgba[3];
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
    hel_log_u32("HEL render3d: model built, vertices", (uint32_t)vertexCount);
    hel_log_u32("HEL render3d: model built, indices", (uint32_t)indexCount);
    X360RuntimeModel *runtimeModel = new X360RuntimeModel(
        mesh, (uint32_t)vertexCount, (uint32_t)indexCount);
    runtimeModel->SetRawModelAsset(data);
    runtimeModel->SetBounds(data->BoundsMin, data->BoundsMax);
    return runtimeModel;
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

    const float width = (float)hel_video_width();
    const float height = (float)hel_video_height();
    const float aspect = height > 0.0f ? width / height : 1.0f;

    for (int32_t index = 0; index < cameraCount; ++index) {
        ::ICamera *camera = cameras->get_Item(index);
        if (camera == nullptr) {
            continue;
        }
        ::Entity *cameraEntity = camera->get_Parent();
        if (cameraEntity == nullptr) {
            hel_log("HEL render3d: camera has no entity, skipped");
            continue;
        }

        /* The view matrix is the inverse of the camera's world transform.
           The generated matrix type exposes the inverse transpose rather
           than the inverse, so transposing that gives the inverse. */
        ::float4x4 worldFromCamera = cameraEntity->get_WorldTransformMatrix();
        ::float4x4 inverseTranspose;
        if (!::float4x4::TryInverseTranspose__ref0_out1(worldFromCamera,
                                                        inverseTranspose)) {
            hel_log("HEL render3d: camera transform is not invertible");
            continue;
        }
        ::float4x4 view;
        ::float4x4::Transpose__ref0_out1(inverseTranspose, view);
        /* The field of view comes from the authored camera, so a scene frames
           the same way here as on any other platform. */
        ::float4x4 projection = ::CameraProjectionUtils::CreatePerspectiveProjection(
            camera, aspect);

        /* The backend composes with column vectors, so projection comes
           first; the conversion transposes each engine matrix. */
        HelMat4 viewProjection = hel_mul(hel_matrix_from_engine(projection),
                                         hel_matrix_from_engine(view));

        ::IRenderQueue3D *queue = camera->get_RenderQueue3D();
        if (queue == nullptr) {
            hel_log("HEL render3d: camera has no render queue");
            continue;
        }
        DrawableVisitor visitor(viewProjection, camera->get_LayerMask(), *this);
        queue->VisitOrdered(&visitor);
        lastDrawnCount_ += visitor.drawn();
        lastSkippedCount_ += visitor.skipped();
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

void X360RenderManager3D::recordDrawError(int error) {
    if (lastDrawError_ == HEL_OK) {
        lastDrawError_ = error;
        hel_log_u32("HEL render3d: backend draw failed with", (uint32_t)error);
    }
}

void X360RenderManager3D::ReleaseModel(::RuntimeModel *model) {
    /* Mesh storage lives until video shutdown (hel_mesh_release_all), so a
       released model only drops its handle. */
    ::RenderManager3D::ReleaseModel(model);
}

} // namespace helengine::x360
