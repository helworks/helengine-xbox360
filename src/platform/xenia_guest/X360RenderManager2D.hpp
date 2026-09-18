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
 *   Sprites and text are counted and not drawn, because both are textured and
 *   this backend has no texture path yet.
 */
#pragma once

#include <cstdint>

#include "RenderManager2D.hpp"
#include "IRenderVisitor2D.hpp"
#include "RuntimeTexture.hpp"

extern "C" {
#include "../../engine/frame.h"
#include "xenia_guest.h"
}

class ICamera;
class TextureAsset;
class IContentStreamSource;

namespace helengine::x360 {

/// <summary>Draws the engine's 2D content through the Xenos backend.</summary>
class X360RenderManager2D final : public ::RenderManager2D,
                                  public ::IRenderVisitor2D {
public:
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
    uint32_t shapesDrawn() const { return shapesDrawn_; }
    uint32_t shapesSkipped() const { return shapesSkipped_; }
    uint32_t spriteRequests() const { return sprites_; }
    uint32_t textRequests() const { return texts_; }

private:
    /// <summary>A texture object carrying only its size, with no pixels.</summary>
    ::RuntimeTexture *describeTexture(int32_t width, int32_t height,
                                      const char *path);

    /// <summary>Submits one batch of pixel-space geometry for the current viewport.</summary>
    void submit(const HelVertex *vertices, uint32_t vertexCount,
                const uint16_t *indices, uint32_t indexCount);

    /* The viewport being drawn into, in pixels. Set once per camera so the
       drawables visited under it convert to clip space against the right
       rectangle. */
    float viewportX_ = 0.0f;
    float viewportY_ = 0.0f;
    float viewportWidth_ = 0.0f;
    float viewportHeight_ = 0.0f;

    uint32_t textures_ = 0;
    uint32_t shapesDrawn_ = 0;
    uint32_t shapesSkipped_ = 0;
    uint32_t sprites_ = 0;
    uint32_t texts_ = 0;
    bool reportedFirstDraw_ = false;
    bool reportedNoTexturePath_ = false;
};

} // namespace helengine::x360
