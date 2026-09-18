#include "X360RenderManager2D.hpp"

#include "AssetSerializer.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable2D.hpp"
#include "IRenderQueue2D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "ObjectManager.hpp"
#include "RoundedRectCorners.hpp"
#include "TextureAsset.hpp"
#include "byte4.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"

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

void fill_vertex(HelVertex *vertex, float x, float y, const float *rgba) {
    vertex->x = x;
    vertex->y = y;
    vertex->z = 0.0f;
    /* Interface geometry faces the viewer and is never lit; the normal is
       present because every vertex carries one. */
    vertex->nx = 0.0f;
    vertex->ny = 0.0f;
    vertex->nz = 1.0f;
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
    const ::float4 viewport = camera->get_Viewport();
    viewportX_ = viewport.X * screenWidth;
    viewportY_ = viewport.Y * screenHeight;
    viewportWidth_ = viewport.Z * screenWidth;
    viewportHeight_ = viewport.W * screenHeight;
    if (viewportWidth_ <= 0.0f || viewportHeight_ <= 0.0f) {
        return;
    }
    queue->VisitOrdered(this);
}

void X360RenderManager2D::endFrame() {
    if (reportedFirstDraw_) {
        return;
    }
    reportedFirstDraw_ = true;
    hel_log_u32("HEL render2d: first draw, shapes drawn", shapesDrawn_);
    hel_log_u32("HEL render2d: first draw, shapes skipped", shapesSkipped_);
    hel_log_u32("HEL render2d: first draw, sprites not drawn", sprites_);
    hel_log_u32("HEL render2d: first draw, text runs not drawn", texts_);
    hel_log_u32("HEL render2d: interface arena peak bytes", hel_ui_peak_bytes());
}

void X360RenderManager2D::Visit(::IDrawable2D *drawable) {
    if (drawable == nullptr) {
        shapesSkipped_++;
        return;
    }
    /* The drawable calls back into one of the Draw* methods below, which is
       how the engine keeps the shape's own fields out of this traversal. */
    drawable->Draw();
}

void X360RenderManager2D::submit(const HelVertex *vertices,
                                 uint32_t vertexCount, const uint16_t *indices,
                                 uint32_t indexCount) {
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

    const int error = hel_draw_ui(vertices, vertexCount, indices, indexCount,
                                  &transform, &params);
    if (error != HEL_OK) {
        shapesSkipped_++;
        return;
    }
    shapesDrawn_++;
}

void X360RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D *shape) {
    if (shape == nullptr || shape->get_Parent() == nullptr) {
        shapesSkipped_++;
        return;
    }
    if (!reportedNoAlphaBlend_) {
        reportedNoAlphaBlend_ = true;
        hel_log("HEL render2d: no alpha blending yet, shapes draw opaque");
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

void X360RenderManager2D::DrawSprite(::ISpriteDrawable2D *sprite) {
    (void)sprite;
    if (!reportedNoTexturePath_) {
        reportedNoTexturePath_ = true;
        hel_log("HEL render2d: sprites and text need textures, none drawn");
    }
    sprites_++;
}

void X360RenderManager2D::DrawText(::ITextDrawable2D *text) {
    (void)text;
    if (!reportedNoTexturePath_) {
        reportedNoTexturePath_ = true;
        hel_log("HEL render2d: sprites and text need textures, none drawn");
    }
    texts_++;
}

::RuntimeTexture *X360RenderManager2D::describeTexture(int32_t width,
                                                       int32_t height,
                                                       const char *path) {
    if (textures_ == 0) {
        hel_log("HEL render2d: textures are described but not uploaded; "
                "this backend has no texture path");
        if (path != nullptr) {
            hel_log(path);
        }
    }
    textures_++;
    ::RuntimeTexture *texture = new ::RuntimeTexture();
    texture->set_Width(width > 0 ? width : 1);
    texture->set_Height(height > 0 ? height : 1);
    texture->set_IsEngineOwned(false);
    return texture;
}

::RuntimeTexture *X360RenderManager2D::BuildTextureFromRaw(::TextureAsset *data) {
    if (data == nullptr) {
        return nullptr;
    }
    return describeTexture((int32_t)data->Width, (int32_t)data->Height, nullptr);
}

::RuntimeTexture *X360RenderManager2D::BuildTextureFromCooked(
    HeCppString cookedAssetPath, ::IContentStreamSource *contentStreamSource) {
    if (contentStreamSource == nullptr) {
        return describeTexture(1, 1, cookedAssetPath.c_str());
    }
    ::Stream *stream = contentStreamSource->OpenRead(cookedAssetPath);
    if (stream == nullptr) {
        hel_log("HEL render2d: cooked texture is not in the image");
        hel_log(cookedAssetPath.c_str());
        return describeTexture(1, 1, cookedAssetPath.c_str());
    }
    ::TextureAsset *asset =
        dynamic_cast<::TextureAsset *>(::AssetSerializer::Deserialize(stream));
    if (asset == nullptr) {
        hel_log("HEL render2d: cooked texture did not deserialize");
        hel_log(cookedAssetPath.c_str());
        return describeTexture(1, 1, cookedAssetPath.c_str());
    }
    return describeTexture((int32_t)asset->Width, (int32_t)asset->Height,
                           cookedAssetPath.c_str());
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
