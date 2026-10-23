#include "X360RenderManager2D.hpp"
#include "Batch2DBuffer.hpp"
#include "Batch2DCameraProjection.hpp"
#include "Batch2DVertex.hpp"
#include "ISpriteDrawable2D.hpp"
#include "Entity.hpp"
#include "ITextDrawable2D.hpp"
#include "X360RuntimeProvider.hpp"
#include <cstddef>
#include <cstring>

extern "C" {
/// Reports the native failure code and exact batch size before the guest's fatal stop.
void hel_log_u32(const char* label, unsigned int value);
}

namespace helengine::x360 {

X360RenderManager2D::~X360RenderManager2D() { Dispose(); }

void X360RenderManager2D::Dispose() {
    if (batchDisposed_) return;
    batchDisposed_ = true;
    delete batchEmitter_;
    batchEmitter_ = nullptr;
    if (batchWriter_) batchWriter_->Dispose();
    delete batchWriter_;
    batchWriter_ = nullptr;
    ::RenderManager2D::Dispose();
}

void X360RenderManager2D::configureBatch() {
    if (batchDisposed_) he_cpp_custom::Fail("Xbox360 batch renderer is disposed");
    if (!batchWriter_) {
        if (hel_shader_program_open("shaders/spritebatch-textured.x3sp", "SpriteBatchShader",
                "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured", 2, &batchProgram_))
            he_cpp_custom::Fail("Xbox360 shared Textured shader artifact is missing or invalid");
        batchDraw_.program = &batchProgram_;
        batchDraw_.constants = batchConstants_;
        batchWriter_ = new ::Batch2DWriter(this, 128);
        batchEmitter_ = new ::Batch2DDrawableEmitter(batchWriter_, this);
    }
    const ::float4 viewport(viewportX_, viewportY_, viewportWidth_, viewportHeight_);
    batchWriter_->ConfigureCamera(::Batch2DCameraProjection::Create(viewport), viewport);
}

void X360RenderManager2D::flushSharedBatch() {
    if (batchWriter_) batchWriter_->Flush();
}

void X360RenderManager2D::SetCamera(::float4x4 projection, ::float4 viewport) {
    if (!(viewport.X >= 0 && viewport.Y >= 0 && viewport.Z > 0 && viewport.W > 0 &&
          viewport.X + viewport.Z <= hel_video_width() && viewport.Y + viewport.W <= hel_video_height()))
        he_cpp_custom::Fail("Xbox360 batch viewport is outside the render target");
    const float rows[] = {static_cast<float>(projection.M11), static_cast<float>(projection.M12), static_cast<float>(projection.M13), static_cast<float>(projection.M14),
        static_cast<float>(projection.M21), static_cast<float>(projection.M22), static_cast<float>(projection.M23), static_cast<float>(projection.M24),
        static_cast<float>(projection.M31), static_cast<float>(projection.M32), static_cast<float>(projection.M33), static_cast<float>(projection.M34),
        static_cast<float>(projection.M41), static_cast<float>(projection.M42), static_cast<float>(projection.M43), static_cast<float>(projection.M44)};
    if (hel_shader_program_projection(&batchProgram_, rows, batchConstants_))
        he_cpp_custom::Fail("Xbox360 batch projection does not match shader constants");
    batchDraw_.viewport[0] = static_cast<uint32_t>(static_cast<double>(viewport.X));
    batchDraw_.viewport[1] = static_cast<uint32_t>(static_cast<double>(viewport.Y));
    batchDraw_.viewport[2] = static_cast<uint32_t>(static_cast<double>(viewport.Z));
    batchDraw_.viewport[3] = static_cast<uint32_t>(static_cast<double>(viewport.W));
}

void X360RenderManager2D::Submit(::Batch2DBuffer *buffer, ::Batch2DRun run) {
    static_assert(sizeof(::Batch2DVertex) == 96, "Shared Batch2D vertex size changed");
    static_assert(offsetof(::Batch2DVertex, Position) == 0 && offsetof(::Batch2DVertex, TexLocal) == 16 &&
        offsetof(::Batch2DVertex, Color) == 32 && offsetof(::Batch2DVertex, Shape) == 48 &&
        offsetof(::Batch2DVertex, Corners) == 64 && offsetof(::Batch2DVertex, BorderColor) == 80,
        "Shared Batch2D attributes changed");
    if (!buffer || !buffer->Vertices || !buffer->Indices || buffer->VertexCount <= 0 || buffer->IndexCount <= 0 ||
        buffer->VertexCount > buffer->Vertices->get_Length() || buffer->IndexCount > buffer->Indices->get_Length() ||
        run.Variant != ::Batch2DVariant::Textured || run.ScissorX < 0 || run.ScissorY < 0 ||
        run.ScissorWidth < 0 || run.ScissorHeight < 0)
        he_cpp_custom::Fail("Xbox360 invalid shared batch submission");
    auto *texture = dynamic_cast<X360RuntimeTexture *>(run.Texture);
    if (!texture || !texture->handle || texture->get_IsDisposed())
        he_cpp_custom::Fail("Xbox360 batch requires a live texture");
    batchDraw_.scissor[0] = (uint32_t)run.ScissorX;
    batchDraw_.scissor[1] = (uint32_t)run.ScissorY;
    batchDraw_.scissor[2] = (uint32_t)run.ScissorWidth;
    batchDraw_.scissor[3] = (uint32_t)run.ScissorHeight;
    // One late native vertex sample checks the animated backdrop after a quarter turn.
    static const uint64_t firstSubmissionTick = hel_ticks();
    static bool lateBackdropReported = false;
    const uint64_t elapsedTicks = hel_ticks() - firstSubmissionTick;
    if (!lateBackdropReported && texture->get_Width() == 512 && texture->get_Height() == 512
        && elapsedTicks / hel_tick_frequency() >= 90u) {
        lateBackdropReported = true;
        const ::float4& first = (*buffer->Vertices)[0].Position;
        const ::float4& opposite = (*buffer->Vertices)[2].Position;
        const float fields[] = {static_cast<float>((first.X + opposite.X) * 0.5f), static_cast<float>((first.Y + opposite.Y) * 0.5f),
            static_cast<float>(first.X), static_cast<float>(first.Y), static_cast<float>(opposite.X), static_cast<float>(opposite.Y)};
        hel_log_u32("HEL late background: elapsed seconds", (uint32_t)(elapsedTicks / hel_tick_frequency()));
        for (float field : fields) {
            uint32_t word;
            std::memcpy(&word, &field, sizeof(word));
            hel_log_u32("HEL late background: center then corners float bits", word);
        }
    }
    // Report the first expanded sprite quad once without changing its submission.
    static bool firstQuadReported = false;
    if (!firstQuadReported) {
        firstQuadReported = true;
        hel_log_u32("HEL batch first quad: texture width", texture->get_Width());
        hel_log_u32("HEL batch first quad: texture height", texture->get_Height());
        hel_log_u32("HEL batch first quad: vertex count", buffer->VertexCount);
        for (int corner = 0; corner < 4; ++corner) {
            const ::Batch2DVertex& vertex = (*buffer->Vertices)[corner];
            const float fields[] = {static_cast<float>(vertex.Position.X), static_cast<float>(vertex.Position.Y), static_cast<float>(vertex.Position.Z), static_cast<float>(vertex.Position.W),
                static_cast<float>(vertex.TexLocal.X), static_cast<float>(vertex.TexLocal.Y), static_cast<float>(vertex.Color.X), static_cast<float>(vertex.Color.Y), static_cast<float>(vertex.Color.Z), static_cast<float>(vertex.Color.W)};
            hel_log_u32("HEL batch first vertex: corner", corner);
            for (float field : fields) {
                uint32_t word;
                std::memcpy(&word, &field, sizeof(word));
                hel_log_u32("HEL batch first vertex: float bits", word);
            }
        }
    }
    const int result = hel_draw_batch(&(*buffer->Vertices)[0], (uint32_t)buffer->VertexCount,
            &(*buffer->Indices)[0], (uint32_t)buffer->IndexCount, texture->handle, &batchDraw_);
    if (result != HEL_OK) {
        hel_log_u32("HEL batch: native submission error", (unsigned int)result);
        hel_log_u32("HEL batch: vertices", (unsigned int)buffer->VertexCount);
        hel_log_u32("HEL batch: indices", (unsigned int)buffer->IndexCount);
        he_cpp_custom::Fail("Xbox360 batch submission failed");
    }
    ++shapesDrawn_;
    if (batchText_) {
        ++textRunsSubmitted_;
        textGlyphsSubmitted_ += (uint32_t)buffer->VertexCount / 4u;
    } else {
        spritesDrawn_ += (uint32_t)buffer->VertexCount / 4u;
    }
}

void X360RenderManager2D::DrawSprite(::ISpriteDrawable2D *sprite) {
    ++sprites_;
    if (sprites_ == 121u || sprites_ == 1201u) {
        auto* parent = sprite->get_Parent();
        if (parent) {
            const ::float3 position = parent->get_Position();
            const ::int2 size = sprite->get_Size();
            hel_log_u32("HEL later sprite: request", sprites_);
            hel_log_u32("HEL later sprite: width", size.X);
            hel_log_u32("HEL later sprite: height", size.Y);
            const float fields[] = {static_cast<float>(position.X), static_cast<float>(position.Y), static_cast<float>(position.Z)};
            for (float field : fields) {
                uint32_t word;
                std::memcpy(&word, &field, sizeof(word));
                hel_log_u32("HEL later sprite: position float bits", word);
            }
        }
    }
    if (!batchEmitter_) he_cpp_custom::Fail("Xbox360 batch camera is not configured");
    if (batchText_) flushSharedBatch();
    batchText_ = false;
    batchEmitter_->EmitSprite(sprite, ::Batch2DRun(::Batch2DVariant::Textured, nullptr,
        (int32_t)activeScissor_.X, (int32_t)activeScissor_.Y,
        (int32_t)activeScissor_.Z, (int32_t)activeScissor_.W));
}

void X360RenderManager2D::DrawText(::ITextDrawable2D *text) {
    ++texts_;
    if (!batchEmitter_) he_cpp_custom::Fail("Xbox360 batch camera is not configured");
    if (!batchText_) flushSharedBatch();
    batchText_ = true;
    batchEmitter_->EmitText(text, ::Batch2DRun(::Batch2DVariant::Textured, nullptr,
        (int32_t)activeScissor_.X, (int32_t)activeScissor_.Y,
        (int32_t)activeScissor_.Z, (int32_t)activeScissor_.W));
}
}
