#include "Batch2DWriter.hpp"
#include "Batch2DSink.hpp"
#include "Batch2DBuffer.hpp"
#include "RuntimeTexture.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace he_cpp_custom {
[[noreturn]] void Fail(const char *message) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
struct Sink final : Batch2DSink {
    std::vector<float> markers;
    unsigned calls = 0, cameras = 0;
    void SetCamera(float4x4, float4) override { ++cameras; }
    void Submit(Batch2DBuffer *buffer, Batch2DRun) override {
        assert(buffer->VertexCount > 0 && buffer->VertexCount <= 128 * 4);
        assert(buffer->IndexCount == buffer->VertexCount / 4 * 6);
        for (int q = 0; q < buffer->VertexCount / 4; ++q) {
            markers.push_back((*buffer->Vertices)[q * 4].Position.X);
            const unsigned short offsets[] = {0, 1, 2, 0, 2, 3};
            for (int i = 0; i < 6; ++i) assert((*buffer->Indices)[q * 6 + i] == q * 4 + offsets[i]);
        }
        ++calls;
    }
};
int main() {
    static_assert(sizeof(Batch2DVertex) == 96);
    Sink sink;
    RuntimeTexture texture;
    Batch2DWriter writer(&sink, 128);
    float4 viewport(0, 0, 640, 480);
    writer.ConfigureCamera(float4x4(), viewport);
    Batch2DRun run(Batch2DVariant::Textured, &texture, 0, 0, 640, 480);
    Batch2DVertex vertex;
    for (unsigned i = 0; i < 300; ++i) {
        vertex.Position = float4(float(i), 0, 0, 1);
        writer.AppendQuad(run, vertex, vertex, vertex, vertex);
    }
    assert(sink.calls == 2 && sink.markers.size() == 256);
    writer.ConfigureCamera(float4x4(), viewport);
    assert(sink.calls == 3 && sink.markers.size() == 300 && sink.cameras == 2);
    for (unsigned i = 0; i < 300; ++i) assert(sink.markers[i] == float(i));
    writer.Flush();
    assert(sink.calls == 3);
    writer.Dispose();
    texture.Dispose();
    puts("Generated shared writer: 300 quads, bounded chunks, copied vertices, indices, camera flush and lifetime passed");
}
