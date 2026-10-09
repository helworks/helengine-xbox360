#include "platform/xenia_guest/x360_native_texture.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Reads managed encoder output rather than reconstructing the same header inside the native test.
static std::vector<std::uint8_t> ReadFixture(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

static std::uint32_t ReadWord(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    assert(offset + 4 <= bytes.size());
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
        (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    constexpr std::uint32_t codes[] = {
        2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18,19,20,22,23,24,25,26,27,28,29,30,31,32,
        36,37,38,49,50,51,52,53,54,55,56,58,59,60,61
    };
    constexpr std::uint32_t widths[] = {3,65,1};
    constexpr std::uint32_t heights[] = {5,33,1};
    static_assert(sizeof(codes) / sizeof(codes[0]) == HEL_X360_NATIVE_TEXTURE_FORMAT_COUNT);
    for (const auto code : codes) {
        for (unsigned dimension = 0; dimension < 3; ++dimension) {
            const auto width = widths[dimension], height = heights[dimension];
            char name[48];
            std::snprintf(name, sizeof(name), "/format_%02X_%ux%u", code, width, height);
            const std::string stem = std::string(argv[1]) + name;
            const auto colors = ReadFixture(stem + ".colors");
            const auto preview = ReadFixture(stem + ".rgba");
            const auto alpha = ReadFixture(stem + ".alpha");
            assert(preview.size() == width * height * 4 && alpha.size() == 1);
            assert(alpha[0] <= 3 || (alpha[0] == 4 && (code == 7 || code == 54)));
            assert(ReadWord(colors, 0) == 0x31543358 && ReadWord(colors, 4) == 1);
            HelX360NativeTextureLayout layout{};
            const std::uint8_t* texels = nullptr;
            assert(hel_x360_native_texture_parse(colors.data(), colors.size(), width, height, &layout, &texels));
            assert(layout.format == code && layout.width == width && layout.height == height);
            assert(layout.byte_length == colors.size() - 56 && texels == colors.data() + 56);
            HelX360NativeTextureFormat format{};
            assert(hel_x360_native_texture_format(code, &format));
            const auto blocks = layout.storage_width / format.block_width;
            assert(blocks % 32 == 0 && layout.row_pitch_bytes % 256 == 0);
            assert(layout.row_pitch_bytes == blocks * format.bytes_per_block);
            assert(layout.byte_length == layout.row_pitch_bytes * ((height + format.block_height - 1) / format.block_height));
            // Check the smallest legal pitch independently of the layout builder.
            assert(layout.storage_width >= width);
            if (blocks > 32) {
                const auto previous = blocks - 32;
                assert(previous * format.block_width < width || (previous * format.bytes_per_block) % 256 != 0);
            }
            std::uint32_t fetch[6]{};
            assert(hel_x360_native_texture_fetch(&layout, 0x01000000, fetch));
            assert((fetch[0] & 3) == 2 && (fetch[0] >> 31) == 0);
            assert(((fetch[0] >> 22) & 511) * 32 == layout.storage_width);
            assert((fetch[1] & 63) == code && ((fetch[1] >> 6) & 3) == 0);
            assert((fetch[1] & 0xfffff000) == 0x01000000);
            assert((fetch[2] & 8191) + 1 == width && ((fetch[2] >> 13) & 8191) + 1 == height);
            assert(((fetch[3] >> 1) & 4095) == ReadWord(colors, 40));
            assert(((fetch[3] >> 23) & 3) == 2 && fetch[4] == 0 && fetch[5] == 512);
            assert(!hel_x360_native_texture_fetch(&layout, 0x20000000, fetch));
            for (const unsigned offset : {0u,4u,8u,12u,16u,20u,24u,28u,32u,36u,40u,44u,48u,52u}) {
                auto damaged = colors;
                damaged[offset] ^= 0x80;
                assert(!hel_x360_native_texture_parse(damaged.data(), damaged.size(), width, height, &layout, &texels));
                assert(texels == nullptr && layout.byte_length == 0);
            }
            assert(!hel_x360_native_texture_parse(colors.data(), colors.size() - 1, width, height, &layout, &texels));
            assert(texels == nullptr);
        }
    }
    std::puts("132 managed native payloads passed C parsing and six-word fetch validation across 44 formats; malformed headers and truncation rejected.");
}
