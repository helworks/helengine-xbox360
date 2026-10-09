#include "X360RenderManager2D.hpp"

#include "AssetSerializer.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "FontAsset.hpp"
#include "FontChar.hpp"
#include "FontInfo.hpp"
#include "ICamera.hpp"
#include "CameraViewportResolver.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable2D.hpp"
#include "IRenderQueue2D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "TextLayoutUtils.hpp"
#include "TextLineOffsets2D.hpp"
#include "TextRenderEffectPass.hpp"
#include "TextRenderEffectPassBuilder.hpp"
#include "TextAlignment.hpp"
#include "system/math.hpp"
#include "ObjectManager.hpp"
#include "RoundedRectCorners.hpp"
#include "TextureAsset.hpp"
#include "TextureAssetColorFormat.hpp"
#include "TextureAssetAlphaPrecision.hpp"
#include "TextureAssetPixelCodec.hpp"
#include "x360_native_texture.h"
#include "byte4.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"
#include "X360RuntimeProvider.hpp"
#include <cmath>

extern "C" {
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
}

namespace {

/* Enough segments that a corner reads as a curve at the sizes an interface
   uses, and few enough that a screen of shapes stays well inside the arena.
   A corner is at most a quarter turn, so six segments put each vertex 15
   degrees apart. */
constexpr int kCornerSegments = 6;
constexpr int kCornerPoints = kCornerSegments + 1;
constexpr int kOutlinePoints = kCornerPoints * 4;

/* Cheap sine and cosine over the quarter turn the corners need. The guest has
   no libm, and the engine's own math header is for the core's translation
   units rather than this one, so the quarter circle is tabulated instead.
   Exactness matters less than that the two outlines of a border ring use the
   same points, which they do because they read the same table. */
struct QuarterTurn {
    float cos[kCornerPoints];
    float sin[kCornerPoints];
};

QuarterTurn build_quarter_turn() {
    QuarterTurn turn = {};
    /* Chebyshev-free: the angles are fixed and few, so each is reached by
       halving and the double-angle identities rather than a series. Starting
       from cos(90/6 degrees) = cos(15 degrees). */
    const float cos15 = 0.96592582628f;
    const float sin15 = 0.25881904510f;
    turn.cos[0] = 1.0f;
    turn.sin[0] = 0.0f;
    for (int index = 1; index < kCornerPoints; ++index) {
        turn.cos[index] = turn.cos[index - 1] * cos15 - turn.sin[index - 1] * sin15;
        turn.sin[index] = turn.sin[index - 1] * cos15 + turn.cos[index - 1] * sin15;
    }
    return turn;
}

const QuarterTurn &quarter_turn() {
    static const QuarterTurn turn = build_quarter_turn();
    return turn;
}

float clampf(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

/* Builds the outline of a rounded rectangle, clockwise from the top-left
   corner, in the same pixel space the caller works in. A corner the mask
   excludes, or one whose radius is zero, collapses to the sharp corner point,
   which keeps the point count fixed so an outer and an inner outline can be
   walked together to make a border ring. */
void build_outline(float left, float top, float right, float bottom,
                   float radius, uint32_t cornerMask, float *outX,
                   float *outY) {
    const QuarterTurn &turn = quarter_turn();
    const float halfWidth = (right - left) * 0.5f;
    const float halfHeight = (bottom - top) * 0.5f;
    const float limit = halfWidth < halfHeight ? halfWidth : halfHeight;
    const float r = clampf(radius, 0.0f, limit < 0.0f ? 0.0f : limit);

    /* Corner order clockwise from the top left, each with the centre of its
       arc, the sign of its sweep, and its flag in RoundedRectCorners. */
    struct Corner {
        float centreX, centreY;
        float startX, startY; /* unit vector at the arc's first point */
        float endX, endY;     /* unit vector at the arc's last point */
        float sharpX, sharpY;
        uint32_t flag;
    };
    const Corner corners[4] = {
        {left + r, top + r, -1.0f, 0.0f, 0.0f, -1.0f, left, top, 1u},     /* top left */
        {right - r, top + r, 0.0f, -1.0f, 1.0f, 0.0f, right, top, 2u},    /* top right */
        {right - r, bottom - r, 1.0f, 0.0f, 0.0f, 1.0f, right, bottom, 8u}, /* bottom right */
        {left + r, bottom - r, 0.0f, 1.0f, -1.0f, 0.0f, left, bottom, 4u},  /* bottom left */
    };

    int point = 0;
    for (int corner = 0; corner < 4; ++corner) {
        const Corner &c = corners[corner];
        const bool rounded = r > 0.0f && (cornerMask & c.flag) != 0u;
        for (int step = 0; step < kCornerPoints; ++step) {
            if (!rounded) {
                outX[point] = c.sharpX;
                outY[point] = c.sharpY;
            } else {
                /* Rotate the start vector towards the end vector by the
                   tabulated angle, which is a quarter turn spread over the
                   segments. */
                const float cosine = turn.cos[step];
                const float sine = turn.sin[step];
                /* The two unit vectors are perpendicular, so a point on the
                   arc is their cosine and sine combination. */
                outX[point] = c.centreX + r * (c.startX * cosine + c.endX * sine);
                outY[point] = c.centreY + r * (c.startY * cosine + c.endY * sine);
            }
            point++;
        }
    }
}

void fill_vertex(HelVertex *vertex, float x, float y, const float *rgba,
                 float u = 0.0f, float v = 0.0f) {
    vertex->x = x;
    vertex->y = y;
    vertex->z = 0.0f;
    /* Interface geometry faces the viewer and is never lit; the normal is
       present because every vertex carries one. */
    vertex->nx = 0.0f;
    vertex->ny = 0.0f;
    vertex->nz = 1.0f;
    vertex->u = u;
    vertex->v = v;
    vertex->r = rgba[0];
    vertex->g = rgba[1];
    vertex->b = rgba[2];
    vertex->a = rgba[3];
}

void colour_from_byte4(const ::byte4 &colour, float *rgba) {
    rgba[0] = (float)colour.X / 255.0f;
    rgba[1] = (float)colour.Y / 255.0f;
    rgba[2] = (float)colour.Z / 255.0f;
    rgba[3] = (float)colour.W / 255.0f;
}

} // namespace

namespace helengine::x360 {

void X360RenderManager2D::beginFrame() {
    shapesDrawn_ = 0;
    shapesSkipped_ = 0;
}

void X360RenderManager2D::renderCamera(::ICamera *camera) {
    flushSharedBatch();
    if (camera == nullptr) {
        return;
    }
    ::IRenderQueue2D *queue = camera->get_RenderQueue2D();
    if (queue == nullptr || queue->get_Count() == 0) {
        return;
    }
    /* The camera's viewport is a fraction of the render target; the engine's
       default is the whole of it. */
    const float screenWidth = (float)hel_video_width();
    const float screenHeight = (float)hel_video_height();
    const ::float4 viewport = ::CameraViewportResolver::ResolveViewport(
        camera->get_Viewport(), screenWidth, screenHeight);
    /* Use one pixel rectangle for projection, hardware viewport and scissor. */
    viewportX_ = clampf((float)::Math::Ceiling((double)viewport.X - 0.5), 0, screenWidth);
    viewportY_ = clampf((float)::Math::Ceiling((double)viewport.Y - 0.5), 0, screenHeight);
    const float right = clampf((float)::Math::Ceiling((double)viewport.X + viewport.Z - 0.5), viewportX_, screenWidth);
    const float bottom = clampf((float)::Math::Ceiling((double)viewport.Y + viewport.W - 0.5), viewportY_, screenHeight);
    viewportWidth_ = right - viewportX_;
    viewportHeight_ = bottom - viewportY_;
    if (viewportWidth_ <= 0.0f || viewportHeight_ <= 0.0f) {
        return;
    }
    configureBatch();
    queue->VisitOrdered(this);
    flushSharedBatch();
}

void X360RenderManager2D::endFrame() {
    flushSharedBatch();
    if (reportedFirstDraw_) {
        return;
    }
    reportedFirstDraw_ = true;
    hel_log_u32("HEL render2d: first draw, shapes drawn", shapesDrawn_);
    hel_log_u32("HEL render2d: first draw, shapes skipped", shapesSkipped_);
    hel_log_u32("HEL render2d: first draw, sprites drawn", spritesDrawn_);
    hel_log_u32("HEL render2d: first draw, sprites requested", sprites_);
    hel_log_u32("HEL render2d: textures uploaded", texturesUploaded_);
    hel_log_u32("HEL render2d: first draw, text runs requested", texts_);
    hel_log_u32("HEL render2d: first draw, shared text batches submitted",
                textRunsSubmitted_);
    hel_log_u32("HEL render2d: first draw, text runs skipped",
                textRunsSkipped_);
    hel_log_u32("HEL render2d: first draw, text quads including effects",
                textGlyphsSubmitted_);
    hel_log_u32("HEL render2d: interface arena peak bytes", hel_ui_peak_bytes());
}

void X360RenderManager2D::Visit(::IDrawable2D *drawable) {
    if (drawable == nullptr) {
        shapesSkipped_++;
        return;
    }
    ::Entity *parent = drawable->get_Parent();
    if (parent == nullptr) he_cpp_custom::Fail("Xbox360 queued drawable has no parent entity");
    if (!parent->get_IsHierarchyEnabled()) {
        ++shapesSkipped_;
        return;
    }
    const ::float4 cameraRect(viewportX_, viewportY_, viewportWidth_, viewportHeight_);
    ::float4 clip = cameraRect;
    clipBuilder_.BuildClipChain(drawable, &clipChain_);
    for (::IClipRegion2D *region : clipChain_) {
        if (region == nullptr) he_cpp_custom::Fail("Xbox360 clip chain contains a null region");
        const ::float4 rect = region->GetClipRect();
        if (!std::isfinite(rect.X) || !std::isfinite(rect.Y) || !std::isfinite(rect.Z) ||
            !std::isfinite(rect.W) || rect.Z < 0.0f || rect.W < 0.0f)
            he_cpp_custom::Fail("Xbox360 authored clip rectangle is invalid");
        clip = clipBuilder_.Intersect(clip, rect);
    }
    const float right = cameraRect.X + cameraRect.Z;
    const float bottom = cameraRect.Y + cameraRect.W;
    const float leftPixel = clampf((float)::Math::Ceiling((double)clip.X - 0.5), cameraRect.X, right);
    const float topPixel = clampf((float)::Math::Ceiling((double)clip.Y - 0.5), cameraRect.Y, bottom);
    const float rightPixel = clampf((float)::Math::Ceiling((double)clip.X + clip.Z - 0.5), leftPixel, right);
    const float bottomPixel = clampf((float)::Math::Ceiling((double)clip.Y + clip.W - 0.5), topPixel, bottom);
    activeScissor_ = ::float4(leftPixel, topPixel, rightPixel - leftPixel, bottomPixel - topPixel);
    if (activeScissor_.Z == 0.0f || activeScissor_.W == 0.0f) {
        ++shapesSkipped_;
        return;
    }
    if (hel_ui_set_scissor((uint32_t)activeScissor_.X, (uint32_t)activeScissor_.Y,
            (uint32_t)activeScissor_.Z, (uint32_t)activeScissor_.W) != HEL_OK)
        he_cpp_custom::Fail("Xbox360 resolved UI scissor is outside the backbuffer");
    /* The drawable calls back into one of the Draw* methods below, which is
       how the engine keeps the shape's own fields out of this traversal. */
    drawable->Draw();
}

bool X360RenderManager2D::submit(const HelVertex *vertices,
                                 uint32_t vertexCount, const uint16_t *indices,
                                 uint32_t indexCount, HelTexture texture) {
    /* Pixels to clip space for the viewport being drawn: X left to right, Y
       top to bottom, which is why the Y row is negated. The backend composes
       with column vectors, so this is stored column by column. */
    HelMat4 transform = {};
    transform.m[0] = 2.0f / viewportWidth_;
    transform.m[5] = -2.0f / viewportHeight_;
    transform.m[10] = 1.0f;
    transform.m[12] = -1.0f - 2.0f * viewportX_ / viewportWidth_;
    transform.m[13] = 1.0f + 2.0f * viewportY_ / viewportHeight_;
    transform.m[15] = 1.0f;

    /* No lighting: ambient at one and no directional term leave the vertex
       colour exactly as authored. */
    HelDrawParams params = {};
    params.base_color[0] = 1.0f;
    params.base_color[1] = 1.0f;
    params.base_color[2] = 1.0f;
    params.base_color[3] = 1.0f;
    params.ambient[0] = 1.0f;
    params.ambient[1] = 1.0f;
    params.ambient[2] = 1.0f;

    const int error =
        hel_draw_ui_textured(vertices, vertexCount, indices, indexCount,
                             &transform, &params, texture);
    if (error != HEL_OK) {
        shapesSkipped_++;
        return false;
    }
    shapesDrawn_++;
    return true;
}

void X360RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D *shape) {
    flushSharedBatch();
    if (shape == nullptr || shape->get_Parent() == nullptr) {
        shapesSkipped_++;
        return;
    }
    /* Interface batches compose in submission order with no depth, so a shape
       that arrives after a sprite covers it. Logging the order each kind
       arrives in is what tells those two apart. */
    if (!reportedOrder_) {
        reportedOrder_ = true;
        hel_log_u32("HEL render2d: a rounded rect arrived at batch",
                    shapesDrawn_ + shapesSkipped_);
    }
    /* The anchor system and the viewport component have already written their
       results into the entity's position and this shape's size, so no layout
       work is needed here. */
    const ::float3 position = shape->get_Parent()->get_Position();
    const ::int2 size = shape->get_Size();
    const float left = position.X;
    const float top = position.Y;
    const float right = left + (float)size.X;
    const float bottom = top + (float)size.Y;
    if (size.X <= 0 || size.Y <= 0) {
        shapesSkipped_++;
        return;
    }

    const uint32_t cornerMask = (uint32_t)shape->get_Corners();
    float outerX[kOutlinePoints];
    float outerY[kOutlinePoints];
    build_outline(left, top, right, bottom, shape->get_Radius(), cornerMask,
                  outerX, outerY);

    float fill[4];
    colour_from_byte4(shape->get_FillColor(), fill);

    /* The fill is a fan from the centre, which is valid because a rounded
       rectangle is convex. */
    HelVertex vertices[kOutlinePoints + 1];
    uint16_t indices[kOutlinePoints * 3];
    fill_vertex(&vertices[0], (left + right) * 0.5f, (top + bottom) * 0.5f,
                fill);
    for (int point = 0; point < kOutlinePoints; ++point) {
        fill_vertex(&vertices[point + 1], outerX[point], outerY[point], fill);
    }
    uint32_t indexCount = 0;
    for (int point = 0; point < kOutlinePoints; ++point) {
        const int next = (point + 1) % kOutlinePoints;
        indices[indexCount++] = 0;
        indices[indexCount++] = (uint16_t)(point + 1);
        indices[indexCount++] = (uint16_t)(next + 1);
    }
    submit(vertices, kOutlinePoints + 1, indices, indexCount);

    /* The border is the ring between that outline and one inset by its
       thickness, walked in step because both have the same point count. */
    const float thickness = shape->get_BorderThickness();
    if (thickness <= 0.0f) {
        return;
    }
    const float halfWidth = (right - left) * 0.5f;
    const float halfHeight = (bottom - top) * 0.5f;
    const float inset = clampf(thickness, 0.0f,
                               halfWidth < halfHeight ? halfWidth : halfHeight);
    float innerX[kOutlinePoints];
    float innerY[kOutlinePoints];
    const float innerRadius = shape->get_Radius() - inset;
    build_outline(left + inset, top + inset, right - inset, bottom - inset,
                  innerRadius < 0.0f ? 0.0f : innerRadius, cornerMask, innerX,
                  innerY);

    float border[4];
    colour_from_byte4(shape->get_BorderColor(), border);
    HelVertex ringVertices[kOutlinePoints * 2];
    uint16_t ringIndices[kOutlinePoints * 6];
    for (int point = 0; point < kOutlinePoints; ++point) {
        fill_vertex(&ringVertices[point * 2], outerX[point], outerY[point],
                    border);
        fill_vertex(&ringVertices[point * 2 + 1], innerX[point], innerY[point],
                    border);
    }
    uint32_t ringIndexCount = 0;
    for (int point = 0; point < kOutlinePoints; ++point) {
        const int next = (point + 1) % kOutlinePoints;
        const uint16_t outerA = (uint16_t)(point * 2);
        const uint16_t innerA = (uint16_t)(point * 2 + 1);
        const uint16_t outerB = (uint16_t)(next * 2);
        const uint16_t innerB = (uint16_t)(next * 2 + 1);
        ringIndices[ringIndexCount++] = outerA;
        ringIndices[ringIndexCount++] = innerA;
        ringIndices[ringIndexCount++] = outerB;
        ringIndices[ringIndexCount++] = innerA;
        ringIndices[ringIndexCount++] = innerB;
        ringIndices[ringIndexCount++] = outerB;
    }
    submit(ringVertices, kOutlinePoints * 2, ringIndices, ringIndexCount);
}

X360RuntimeTexture::~X360RuntimeTexture() { Dispose(); }

void X360RuntimeTexture::Dispose() {
    if (get_IsDisposed()) return;
    if (handle != 0) {
        hel_texture_release(handle);
        handle = 0;
    }
    ::RuntimeTexture::Dispose();
}

::RuntimeTexture *X360RenderManager2D::buildTexture(int32_t width,
                                                    int32_t height,
                                                    const uint8_t *rgba,
                                                    const char *path) {
    textures_++;
    X360RuntimeTexture *texture = new X360RuntimeTexture();
    /* The size is reported even when no pixels are uploaded, because layout
       and sprite sizing divide by it. A texture with no handle simply does
       not draw. */
    texture->set_Width(width > 0 ? width : 1);
    texture->set_Height(height > 0 ? height : 1);
    texture->set_IsEngineOwned(false);
    if (rgba != nullptr && width > 0 && height > 0) {
        const int error = hel_texture_create(rgba, (uint32_t)width,
                                             (uint32_t)height,
                                             &texture->handle);
        if (error != HEL_OK) {
            texture->handle = 0;
            hel_log_u32("HEL render2d: texture upload failed with",
                        (uint32_t)error);
            if (path != nullptr) {
                hel_log(path);
            }
        } else {
            texturesUploaded_++;
        }
    }
    return texture;
}

::RuntimeTexture *X360RenderManager2D::buildFromAsset(::TextureAsset *asset,
                                                      const char *path) {
    if (asset == nullptr) {
        return buildTexture(1, 1, nullptr, path);
    }
    const int32_t width = (int32_t)asset->Width;
    const int32_t height = (int32_t)asset->Height;
    if (!asset->Width || !asset->Height || asset->Width > 8192 || asset->Height > 8192 || asset->Colors == nullptr) {
        hel_log("HEL render2d: texture has invalid dimensions or no pixel payload");
        return buildTexture(width,height,nullptr,path);
    }
    if ((uint32_t)asset->ColorFormat == HEL_X360_NATIVE_COLOR_FORMAT) {
        textures_++;
        X360RuntimeTexture *texture = new X360RuntimeTexture();
        texture->set_Width(width);
        texture->set_Height(height);
        texture->set_IsEngineOwned(false);
        const bool noPalette = asset->PaletteColors == nullptr || asset->PaletteColors->get_Length() == 0;
        const int error = noPalette && (uint32_t)asset->AlphaPrecision <= 3u && asset->Colors->get_Length() > 0
            ? hel_texture_create_native(asset->Colors->Data,(uint32_t)asset->Colors->get_Length(),
                asset->Width,asset->Height,&texture->handle) : HEL_ERR_MESH_INVALID;
        if (error != HEL_OK) {
            hel_log_u32("HEL render2d: native texture rejected or allocation failed",(uint32_t)error);
            if (path != nullptr) hel_log(path);
        } else {
            texturesUploaded_++;
        }
        return texture;
    }
    // Inline legacy font atlases are anonymous RGBA/Opaque defaults whose alpha is actual glyph coverage.
    // Preserve those bytes without changing the authoring asset or the policy of identified textures.
    const bool anonymousFontCoverage = hel_x360_preserve_anonymous_font_alpha(
        (uint32_t)asset->ColorFormat,(uint32_t)asset->AlphaPrecision,
        !asset->Id.empty() || asset->RuntimeAssetId != 0 || !asset->AuthoringAssetId.empty());
    if (asset->ColorFormat == ::TextureAssetColorFormat::Rgba32 && asset->Colors->get_Length() < width * height * 4) {
        if (!reportedNoTexturePath_) {
            reportedNoTexturePath_ = true;
            hel_log("HEL render2d: cooked RGBA texture payload is truncated, so its pixels are not uploaded");
            hel_log_u32("HEL render2d: its colour format",
                        (uint32_t)asset->ColorFormat);
            if (path != nullptr) {
                hel_log(path);
            }
        }
        return buildTexture(width, height, nullptr, path);
    }
    if (anonymousFontCoverage) return buildTexture(width,height,asset->Colors->Data,path);
    // The shared codec returns a distinct owned array for every format; free the temporary after the physical upload.
    Array<uint8_t> *rgba = ::TextureAssetPixelCodec::DecodeToRgba32(asset);
    ::RuntimeTexture *texture = buildTexture(width,height,rgba->Data,path);
    delete rgba;
    return texture;
}

::RuntimeTexture *X360RenderManager2D::BuildTextureFromRaw(::TextureAsset *data) {
    if (data == nullptr) {
        return nullptr;
    }
    return buildFromAsset(data, nullptr);
}

::RuntimeTexture *X360RenderManager2D::BuildTextureFromCooked(
    HeCppString cookedAssetPath, ::IContentStreamSource *contentStreamSource) {
    if (contentStreamSource == nullptr) {
        return buildTexture(1, 1, nullptr, cookedAssetPath.c_str());
    }
    ::Stream *stream = contentStreamSource->OpenRead(cookedAssetPath);
    if (stream == nullptr) {
        hel_log("HEL render2d: cooked texture is not in the image");
        hel_log(cookedAssetPath.c_str());
        return buildTexture(1, 1, nullptr, cookedAssetPath.c_str());
    }
    ::Asset *deserialized = ::AssetSerializer::Deserialize(stream);
    stream->Dispose();
    delete stream;
    ::TextureAsset *asset = dynamic_cast<::TextureAsset *>(deserialized);
    if (asset == nullptr) {
        hel_log("HEL render2d: cooked texture did not deserialize");
        hel_log(cookedAssetPath.c_str());
        delete deserialized;
        return buildTexture(1, 1, nullptr, cookedAssetPath.c_str());
    }
    // Upload copies the pixels into physical GPU memory; the deserialized CPU
    // array is temporary and must not survive another scene load.
    ::RuntimeTexture *texture = buildFromAsset(asset, cookedAssetPath.c_str());
    hel_log_u32("HEL render2d: upload heap before temporary disposal", hel_heap_used_bytes());
    asset->Dispose();
    delete asset;
    hel_log_u32("HEL render2d: upload heap after temporary disposal", hel_heap_used_bytes());
    return texture;
}

void X360RenderManager2D::UpdateTextureRegionCore(
    ::RuntimeTexture *texture, int32_t x, int32_t y, int32_t width,
    int32_t height, Array<uint8_t> *rgba8, int32_t sourceRowPitch) {
    /* Nothing to update: no pixels were ever uploaded. */
    (void)texture;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)rgba8;
    (void)sourceRowPitch;
}

} // namespace helengine::x360
