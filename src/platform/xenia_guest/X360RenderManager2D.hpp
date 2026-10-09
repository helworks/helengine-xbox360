/*
 * The engine's 2D renderer for this console.
 *
 * The engine never calls RenderManager2D::Draw. Its own frame calls only the
 * 3D manager, so a 2D pass has to be driven from there: X360RenderManager3D
 * calls renderCamera below once per camera, after it has drawn that camera's
 * 3D content, which is what the Windows backend does and what makes the
 * interface compose over the scene in the right order.
 *
 * renderCamera visits the camera's 2D queue in order and lets each drawable
 * call back into the DrawRoundedRect, DrawSprite and DrawText below.
 *
 * A 2D drawable needs no layout work here. The anchor system and the viewport
 * component have already written their results into the entity's position and
 * the drawable's size by the time this runs, so the rectangle to draw is the
 * entity's world position and the drawable's size, in pixels, with the origin
 * at the top left and Y increasing downwards.
 *
 * What this draws and what it does not:
 *   Rounded rectangles are triangulated and drawn, blended over the scene.
 *   Sprites and text are drawn when their textures were in a format this
 *   backend converts. Text uses the glyph atlas already cooked into FontAsset.
 */
#pragma once

#include <cstdint>

#include "RenderManager2D.hpp"
#include "IRenderVisitor2D.hpp"
#include "RuntimeTexture.hpp"
#include "Batch2DSink.hpp"
#include "Batch2DWriter.hpp"
#include "Batch2DDrawableEmitter.hpp"
#include "ClipRegionStackBuilder2D.hpp"
#include "IClipRegion2D.hpp"

extern "C" {
#include "../../engine/frame.h"
#include "xenia_guest.h"
}

class ICamera;
class TextureAsset;
class ISpriteDrawable2D;
class IContentStreamSource;

namespace helengine::x360 {

/// <summary>
/// A texture the engine holds, and the backend handle its pixels were
/// uploaded to.
/// </summary>
/// <remarks>
/// A handle of zero means upload failed validation or physical allocation. Native
/// payloads preserve their packed GPU representation; generic formats are decoded by the shared codec. The size is still
/// real, so layout and sprite sizing work; what is missing is only the
/// picture, and the log says which format it was.
/// </remarks>
class X360RuntimeTexture final : public ::RuntimeTexture {
public:
    /// Releases this texture's owned backend allocation through the next GPU fence.
    ~X360RuntimeTexture() override;
    /// Retires the upload once; repeated disposal and shutdown cannot double-free it.
    void Dispose() override;
    /// Backend handle owned exclusively by this runtime texture until retirement.
    HelTexture handle = 0;
};

/// <summary>Draws the engine's 2D content through the Xenos backend.</summary>
class X360RenderManager2D final : public ::RenderManager2D,
                                  public ::IRenderVisitor2D, public ::Batch2DSink {
public:
    ~X360RenderManager2D() override;
    void Dispose() override;
    void SetCamera(::float4x4 projection, ::float4 viewport) override;
    void Submit(::Batch2DBuffer *buffer, ::Batch2DRun run) override;
    /// <summary>Draws one camera's 2D queue. Called by the 3D manager.</summary>
    /// <param name="camera">Camera whose queue and viewport to use.</param>
    void renderCamera(::ICamera *camera);

    /// <summary>Clears the per-frame counters. Called once before the cameras.</summary>
    void beginFrame();

    /// <summary>Reports the first frame's counters. Called once after the cameras.</summary>
    void endFrame();

    void Visit(::IDrawable2D *drawable) override;

    void DrawRoundedRect(::IRoundedRectDrawable2D *shape) override;
    void DrawSprite(::ISpriteDrawable2D *sprite) override;
    void DrawText(::ITextDrawable2D *text) override;

    ::RuntimeTexture *BuildTextureFromRaw(::TextureAsset *data) override;
    ::RuntimeTexture *BuildTextureFromCooked(
        HeCppString cookedAssetPath,
        ::IContentStreamSource *contentStreamSource) override;

    void UpdateTextureRegionCore(::RuntimeTexture *texture, int32_t x,
                                 int32_t y, int32_t width, int32_t height,
                                 Array<uint8_t> *rgba8,
                                 int32_t sourceRowPitch) override;

    uint32_t textureCount() const { return textures_; }
    uint32_t texturesUploaded() const { return texturesUploaded_; }
    uint32_t spritesDrawn() const { return spritesDrawn_; }
    uint32_t shapesDrawn() const { return shapesDrawn_; }
    uint32_t shapesSkipped() const { return shapesSkipped_; }
    uint32_t spriteRequests() const { return sprites_; }
    uint32_t textRequests() const { return texts_; }
    uint32_t textRunsSubmitted() const { return textRunsSubmitted_; }
    uint32_t textRunsSkipped() const { return textRunsSkipped_; }
    uint32_t textGlyphsSubmitted() const { return textGlyphsSubmitted_; }

private:
    /// Uses Core's authored ancestry to resolve enabled clip components for each drawable.
    ::ClipRegionStackBuilder2D clipBuilder_;
    /// Reused chain storage avoids allocating an ancestor list for every menu row.
    ::List<::IClipRegion2D *> clipChain_;
    /// Pixel-center-bounded intersection of the camera and current authored clip chain.
    ::float4 activeScissor_;
    ::Batch2DWriter *batchWriter_ = nullptr;
    ::Batch2DDrawableEmitter *batchEmitter_ = nullptr;
    HelShaderProgram batchProgram_ = {};
    HelBatchDraw batchDraw_ = {};
    uint32_t batchConstants_[16] = {};
    bool batchDisposed_ = false;
    bool batchText_ = false;
    void configureBatch();
    void flushSharedBatch();

    /// <summary>Builds the engine's texture object, uploading pixels when it can.</summary>
    /// <param name="width">Texture width in pixels.</param>
    /// <param name="height">Texture height in pixels.</param>
    /// <param name="rgba">Pixels, or null when the format was not convertible.</param>
    /// <param name="path">Asset path, for the log; may be null.</param>
    ::RuntimeTexture *buildTexture(int32_t width, int32_t height,
                                   const uint8_t *rgba, const char *path);

    /// <summary>Converts a cooked texture's pixels, or reports why it cannot.</summary>
    ::RuntimeTexture *buildFromAsset(::TextureAsset *asset, const char *path);

    /// <summary>Submits one batch of pixel-space geometry for the current viewport.</summary>
    /// <param name="texture">Backend texture to sample, or zero for none.</param>
    /// <returns>True when the batch reached the ring.</returns>
    bool submit(const HelVertex *vertices, uint32_t vertexCount,
                const uint16_t *indices, uint32_t indexCount,
                HelTexture texture = 0);


    /* The viewport being drawn into, in pixels. Set once per camera so the
       drawables visited under it convert to clip space against the right
       rectangle. */
    float viewportX_ = 0.0f;
    float viewportY_ = 0.0f;
    float viewportWidth_ = 0.0f;
    float viewportHeight_ = 0.0f;

    uint32_t textures_ = 0;
    uint32_t texturesUploaded_ = 0;
    uint32_t shapesDrawn_ = 0;
    uint32_t shapesSkipped_ = 0;
    uint32_t sprites_ = 0;
    uint32_t spritesDrawn_ = 0;
    uint32_t texts_ = 0;
    uint32_t textRunsSubmitted_ = 0;
    uint32_t textRunsSkipped_ = 0;
    uint32_t textGlyphsSubmitted_ = 0;
    bool reportedFirstDraw_ = false;
    bool reportedNoTexturePath_ = false;
    bool reportedMissingTextFont_ = false;
    bool reportedOrder_ = false;
};

} // namespace helengine::x360
