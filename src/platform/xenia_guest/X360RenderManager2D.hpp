/*
 * The engine's 2D renderer for this console.
 *
 * RenderManager2D::Draw is an empty virtual in the engine, so the platform
 * owns the traversal, the same as in 3D. This walks the cameras the object
 * manager holds in draw order, visits each one's 2D queue in order, and lets
 * each drawable call back into the DrawRoundedRect, DrawSprite and DrawText
 * below.
 *
 * A 2D drawable needs no layout work here. The anchor system and the viewport
 * component have already written their results into the entity's position and
 * the drawable's size by the time this runs, so the rectangle to draw is the
 * entity's world position and the drawable's size, in pixels, with the origin
 * at the top left and Y increasing downwards.
 *
 * What this draws and what it does not:
 *   Rounded rectangles are triangulated and drawn.
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

class TextureAsset;
class IContentStreamSource;

namespace helengine::x360 {

/// <summary>Draws the engine's 2D content through the Xenos backend.</summary>
class X360RenderManager2D final : public ::RenderManager2D,
                                  public ::IRenderVisitor2D {
public:
    void Draw() override;
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
    bool reportedNoAlphaBlend_ = false;
};

} // namespace helengine::x360
