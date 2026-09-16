/*
 * Builds a scene with the engine's own types and drives it each frame.
 *
 * Nothing here is cooked content: the model is a ModelAsset filled in code,
 * and the entities, camera and mesh component are the engine's, created
 * through its own constructors. That is the point of this stage. The
 * renderer (X360RenderManager3D) then draws the scene by walking the
 * engine's camera and render queue, so the whole path from an engine scene
 * to pixels on the console is exercised without a content pipeline.
 *
 * The order below follows what the engine requires, and two steps are easy
 * to miss: an entity's component and child lists must be initialised before
 * AddComponent is legal, and a component registers itself with the object
 * manager at the moment it is added, which is why the entity must already be
 * enabled by then.
 */
#include "CameraComponent.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "MeshComponent.hpp"
#include "ModelAsset.hpp"
#include "RuntimeModel.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "runtime/array.hpp"

#include "../platform/xenia_guest/X360RenderManager3D.hpp"
#include "core_host.hpp"

extern "C" {
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
}

namespace {

::Entity *g_cube_entity;
::MeshComponent *g_cube_mesh;
::Entity *g_camera_entity;
float g_angle;

::float3 make_float3(float x, float y, float z) {
    ::float3 value;
    value.X = x;
    value.Y = y;
    value.Z = z;
    return value;
}

/* A unit cube with four vertices per face, so every face carries its own
   normal. Faces are wound counter-clockwise seen from outside, which is the
   winding the backend's culling was verified against. */
::ModelAsset *build_cube_asset() {
    static const float kFaceNormals[6][3] = {
        {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f},
    };
    /* Corner offsets for each face, in counter-clockwise order from
       outside: right, left, top, bottom, front, back. */
    static const float kFaceCorners[6][4][3] = {
        {{0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
        {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}},
        {{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}},
        {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}},
        {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}},
        {{0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}},
    };

    ::ModelAsset *asset = new ::ModelAsset();
    asset->Positions = new Array<::float3>(24);
    asset->Normals = new Array<::float3>(24);
    asset->Indices16 = new Array<uint16_t>(36);
    for (int face = 0; face < 6; ++face) {
        for (int corner = 0; corner < 4; ++corner) {
            const int vertex = face * 4 + corner;
            (*asset->Positions)[vertex] =
                make_float3(kFaceCorners[face][corner][0],
                            kFaceCorners[face][corner][1],
                            kFaceCorners[face][corner][2]);
            (*asset->Normals)[vertex] =
                make_float3(kFaceNormals[face][0], kFaceNormals[face][1],
                            kFaceNormals[face][2]);
        }
        const int base = face * 6;
        const uint16_t first = (uint16_t)(face * 4);
        (*asset->Indices16)[base + 0] = first;
        (*asset->Indices16)[base + 1] = (uint16_t)(first + 1);
        (*asset->Indices16)[base + 2] = (uint16_t)(first + 2);
        (*asset->Indices16)[base + 3] = first;
        (*asset->Indices16)[base + 4] = (uint16_t)(first + 2);
        (*asset->Indices16)[base + 5] = (uint16_t)(first + 3);
    }
    asset->BoundsMin = make_float3(-0.5f, -0.5f, -0.5f);
    asset->BoundsMax = make_float3(0.5f, 0.5f, 0.5f);
    asset->set_Id(HeCppString("x360-debug-cube"));
    return asset;
}

} // namespace

extern "C" int hel_core_scene_build(void) {
    ::Core *core = hel_core_instance();
    helengine::x360::X360RenderManager3D *render3d = hel_core_render3d();
    if (core == nullptr || render3d == nullptr) {
        hel_log("HEL scene: no core to build into");
        return HEL_ERR_VIDEO_NOT_READY;
    }
    hel_log("HEL scene: building");

    /* Camera. Its entity's transform is the camera's world transform, which
       the renderer inverts to get the view matrix; the default orientation
       looks along -Z, so the camera sits on +Z to see the origin. */
    g_camera_entity = new ::Entity(core);
    g_camera_entity->InitComponents();
    g_camera_entity->InitChildren();
    g_camera_entity->set_LocalPosition(make_float3(0.0f, 0.0f, 3.0f));
    ::CameraComponent *camera = new ::CameraComponent();
#if defined(HEL_SCENE_FIELD_OF_VIEW_MILLIRADIANS)
    /* An authored field of view, to show the renderer reads the camera's
       value rather than choosing its own. */
    camera->set_FieldOfView((float)(HEL_SCENE_FIELD_OF_VIEW_MILLIRADIANS) / 1000.0f);
#endif
    hel_log_u32("HEL scene: camera field of view milliradians",
                (unsigned int)(camera->get_FieldOfView() * 1000.0f));
    g_camera_entity->AddComponent(camera);
    g_camera_entity->InitializeHierarchy();

    /* The model, built in code and uploaded through the renderer's own
       BuildModelFromRaw, which is the hook the engine provides for a
       platform with no content pipeline yet. */
    ::ModelAsset *asset = build_cube_asset();
    ::RuntimeModel *model = render3d->BuildModelFromRaw(asset);
    if (model == nullptr) {
        hel_log("HEL scene: the renderer could not build the cube model");
        return HEL_ERR_MESH_INVALID;
    }

    g_cube_entity = new ::Entity(core);
    g_cube_entity->InitComponents();
    g_cube_entity->InitChildren();
    g_cube_entity->set_LocalPosition(make_float3(0.0f, 0.0f, 0.0f));
    g_cube_mesh = new ::MeshComponent();
    g_cube_mesh->set_Model(model);
    /* No materials: the engine allows a drawable with an empty material
       array, and the renderer colours vertices from their normals until
       materials are wired up. */
    g_cube_entity->AddComponent(g_cube_mesh);
    g_cube_entity->InitializeHierarchy();

    hel_log("HEL scene: built one camera and one cube");
    return HEL_OK;
}

extern "C" void hel_core_scene_update(float elapsed_seconds) {
    if (g_cube_entity == nullptr) {
        return;
    }
    /* Spin about +Y at 0.5 rad/s through the engine's own transform, so the
       rotation the renderer draws came from the scene rather than from the
       backend. -DHEL_SCENE_SPIN=0 holds it still, which is what the
       measurement captures use. */
#if defined(HEL_SCENE_SPIN) && (HEL_SCENE_SPIN == 0)
    (void)elapsed_seconds;
#else
    g_angle += elapsed_seconds * 0.5f;
#endif
    while (g_angle > 6.28318531f) {
        g_angle -= 6.28318531f;
    }
    ::float3 axis = make_float3(0.0f, 1.0f, 0.0f);
    ::float4 orientation;
    ::float4::CreateFromAxisAngle__ref0_out2(axis, g_angle, orientation);
    g_cube_entity->set_LocalOrientation(orientation);
}

extern "C" unsigned int hel_core_scene_angle_milliradians(void) {
    return (unsigned int)(g_angle * 1000.0f);
}
