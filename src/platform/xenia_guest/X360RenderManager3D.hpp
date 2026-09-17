/*
 * The engine's 3D renderer for this console, and the runtime model it hands
 * back. See X360RenderManager3D.cpp for how a frame is drawn.
 */
#pragma once

#include <cstdint>

#include "Core.hpp"
#include "RenderManager3D.hpp"
#include "RuntimeModel.hpp"

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

/// <summary>Draws the engine's scene through the Xenos backend.</summary>
class X360RenderManager3D final : public ::RenderManager3D {
public:
    ::RuntimeModel *BuildModelFromRaw(::ModelAsset *data) override;
    void Draw() override;
    void ReleaseModel(::RuntimeModel *model) override;
    /* The engine reads this to report how much the renderer drew. */
    int32_t get_LastDrawCallCount() override;

    /// <summary>Records the first backend failure of the current frame.</summary>
    void recordDrawError(int error);

    uint32_t modelCount() const { return models_; }
    uint32_t lastDrawnCount() const { return lastDrawnCount_; }
    uint32_t lastSkippedCount() const { return lastSkippedCount_; }
    int lastDrawError() const { return lastDrawError_; }

private:
    uint32_t models_ = 0;
    uint32_t lastDrawnCount_ = 0;
    uint32_t lastSkippedCount_ = 0;
    int lastDrawError_ = 0;
    bool reportedFirstDraw_ = false;
};

} // namespace helengine::x360
