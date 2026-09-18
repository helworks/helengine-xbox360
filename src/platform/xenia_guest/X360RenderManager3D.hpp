/*
 * The engine's 3D renderer for this console, and the runtime model it hands
 * back. See X360RenderManager3D.cpp for how a frame is drawn.
 */
#pragma once

#include <cstdint>

#include "Core.hpp"
#include "RenderManager3D.hpp"
#include "RuntimeModel.hpp"
#include "RuntimeMaterial.hpp"
/* Says whether the generated core resolves materials as cooked platform-owned
   payloads, which is what decides whether the cooked entry points below exist
   to override. */
#include "helcpp_config.hpp"

class ICamera;
class ObjectManager;
class PlatformMaterialAsset;

extern "C" {
#include "../../engine/frame.h"
#include "xenia_guest.h"
}

namespace helengine::x360 {

/// <summary>A model uploaded to the backend's mesh storage.</summary>
class X360RuntimeModel final : public ::RuntimeModel {
public:
    X360RuntimeModel(HelMesh mesh, uint32_t vertexCount, uint32_t indexCount);

    /// <summary>Backend mesh handle, zero when the upload failed.</summary>
    HelMesh mesh() const { return mesh_; }
    uint32_t vertexCount() const { return vertexCount_; }
    uint32_t indexCount() const { return indexCount_; }

private:
    HelMesh mesh_ = 0;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
};

/// <summary>
/// A cooked material, carrying the one value this renderer can act on.
/// </summary>
/// <remarks>
/// The cooker rejects textures, lighting and transparency by name, so the base
/// colour is all that survives cooking and all that is kept here. It reaches
/// the GPU as a per-draw constant that the vertex shader multiplies the
/// lighting by.
/// </remarks>
class X360RuntimeMaterial final : public ::RuntimeMaterial {
public:
    /// <summary>The cooked base colour, packed 0xRRGGBBAA.</summary>
    uint32_t baseColor = 0xFFFFFFFFu;
};

/// <summary>Draws the engine's scene through the Xenos backend.</summary>
class X360RenderManager3D final : public ::RenderManager3D {
public:
    ::RuntimeModel *BuildModelFromRaw(::ModelAsset *data) override;
    ::RuntimeModel *BuildModelFromCooked(
        HeCppString cookedAssetPath,
        ::IContentStreamSource *contentStreamSource) override;
#if defined(HELENGINE_RUNTIME_MATERIAL_RESOLUTION_COOKED_PLATFORM_OWNED)
    /* Present only when the core was generated with cooked platform-owned
       materials. A core generated without them has no such entry point, and
       declaring an override for a method that does not exist is an error, so
       the guard is what lets one renderer serve both. */
    ::RuntimeMaterial *BuildMaterialFromCooked(
        ::PlatformMaterialAsset *materialAsset) override;
    ::RuntimeMaterial *BuildMaterialFromCooked(
        HeCppString cookedAssetPath,
        ::IContentStreamSource *contentStreamSource) override;
#endif
    void Draw() override;
    void ReleaseModel(::RuntimeModel *model) override;
    /* The engine reads this to report how much the renderer drew. */
    int32_t get_LastDrawCallCount() override;

    /// <summary>Records the first backend failure of the current frame.</summary>
    void recordDrawError(int error);

    /// <summary>
    /// The authored background colour, from the lowest-draw-order camera that
    /// clears colour.
    /// </summary>
    /// <param name="rgba">Receives the colour; untouched when none is authored.</param>
    /// <returns>True when a camera asked for a colour clear.</returns>
    bool resolveClearColor(float *rgba);

    /// <summary>Draws one camera's 3D content.</summary>
    /// <param name="camera">Camera to draw.</param>
    /// <param name="lighting">Lighting constants resolved for this frame.</param>
    /// <param name="aspect">Render target aspect ratio.</param>
    void drawCamera3D(::ICamera *camera, const HelDrawParams &lighting,
                      float aspect);

    /// <summary>Resolves the scene's lights into the constants a draw shades with.</summary>
    HelDrawParams resolveLighting(::ObjectManager *objectManager);

    uint32_t modelCount() const { return models_; }
    uint32_t materialCount() const { return materials_; }
    uint32_t lastDrawnCount() const { return lastDrawnCount_; }
    uint32_t lastSkippedCount() const { return lastSkippedCount_; }
    int lastDrawError() const { return lastDrawError_; }

private:
    uint32_t models_ = 0;
    uint32_t materials_ = 0;
    uint32_t lastDrawnCount_ = 0;
    uint32_t lastSkippedCount_ = 0;
    int lastDrawError_ = 0;
    bool reportedFirstDraw_ = false;
    bool reportedClearColor_ = false;
};

} // namespace helengine::x360
