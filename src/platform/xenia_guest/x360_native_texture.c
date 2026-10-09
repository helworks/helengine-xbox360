#include "x360_native_texture.h"
#include "xenos_regs.h"

/* These are the pinned Xenos surface-format codes, not an invented sequential public format enum. */
static const HelX360NativeTextureFormat formats[] = {
    {2,1,1,1,0xA00}, {3,1,1,2,0x688}, {4,1,1,2,0xA88}, {5,1,1,2,0xA88},
    {6,1,1,4,0x688}, {7,1,1,4,0x688}, {8,1,1,1,0x16D}, {9,1,1,1,0xA00},
    {10,1,1,2,0xB08}, {11,2,1,4,0xA88}, {12,2,1,4,0xA88},
    {14,1,1,4,0x688}, {15,1,1,2,0x688}, {16,1,1,4,0xA88}, {17,1,1,4,0xA88},
    {18,4,4,8,0x688}, {19,4,4,16,0x688}, {20,4,4,16,0x688},
    {22,1,1,4,0xA00}, {23,1,1,4,0xA00}, {24,1,1,2,0xA00}, {25,1,1,4,0xB08},
    {26,1,1,8,0x688}, {27,1,1,2,0xA00}, {28,1,1,4,0xB08}, {29,1,1,8,0x688},
    {30,1,1,2,0xA00}, {31,1,1,4,0xB08}, {32,1,1,8,0x688},
    {36,1,1,4,0xA00}, {37,1,1,8,0xB08}, {38,1,1,16,0x688},
    {49,4,4,16,0xB08}, {50,1,1,4,0x688}, {51,4,4,8,0x688},
    {52,4,4,16,0x688}, {53,4,4,16,0x688}, {54,1,1,4,0x688},
    {55,1,1,4,0xA88}, {56,1,1,4,0xA88},
    {58,4,4,8,0x16D}, {59,4,4,8,0x16D}, {60,4,4,8,0xB08}, {61,4,4,8,0x688}
};
_Static_assert(sizeof(formats) / sizeof(formats[0]) == HEL_X360_NATIVE_TEXTURE_FORMAT_COUNT,
               "Xenos texture format count must match the managed catalog");

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int hel_x360_preserve_anonymous_font_alpha(uint32_t color_format, uint32_t alpha_precision, int has_stable_identity) {
    return color_format == 0 && alpha_precision == 0 && !has_stable_identity;
}

int hel_x360_native_texture_format(uint32_t code, HelX360NativeTextureFormat *out) {
    if (!out) return 0;
    *out = (HelX360NativeTextureFormat){0};
    for (uint32_t index = 0; index < HEL_X360_NATIVE_TEXTURE_FORMAT_COUNT; ++index) {
        if (formats[index].code == code) { *out = formats[index]; return 1; }
    }
    return 0;
}

int hel_x360_native_texture_layout(uint32_t code, uint32_t width, uint32_t height,
                                   HelX360NativeTextureLayout *out) {
    if (!out) return 0;
    *out = (HelX360NativeTextureLayout){0};
    HelX360NativeTextureFormat format;
    if (!width || !height || width > 8192 || height > 8192 ||
        !hel_x360_native_texture_format(code, &format)) return 0;
    const uint32_t alignment = format.block_width * 32u;
    uint32_t pitch = ((width + alignment - 1u) / alignment) * alignment;
    uint32_t row_bytes = (pitch / format.block_width) * format.bytes_per_block;
    while (row_bytes % 256u) {
        pitch += alignment;
        row_bytes = (pitch / format.block_width) * format.bytes_per_block;
    }
    if (pitch / 32u > 511u) return 0;
    const uint32_t rows = (height + format.block_height - 1u) / format.block_height;
    const uint64_t byte_length = (uint64_t)row_bytes * rows;
    if (byte_length > UINT32_MAX - HEL_X360_NATIVE_TEXTURE_HEADER_BYTES) return 0;
    *out = (HelX360NativeTextureLayout){code,width,height,pitch,rows * format.block_height,
        row_bytes,(uint32_t)byte_length,format.swizzle,0,0,0};
    return 1;
}

int hel_x360_native_texture_parse(const uint8_t *colors, uint32_t colors_length,
    uint32_t expected_width, uint32_t expected_height,
    HelX360NativeTextureLayout *out, const uint8_t **out_texels) {
    if (out) *out = (HelX360NativeTextureLayout){0};
    if (out_texels) *out_texels = 0;
    if (!out || !out_texels || !colors || colors_length < HEL_X360_NATIVE_TEXTURE_HEADER_BYTES ||
        read_le32(colors) != 0x31543358u || read_le32(colors + 4) != 1 || read_le32(colors + 12) != 0 ||
        read_le32(colors + 16) != expected_width || read_le32(colors + 20) != expected_height) return 0;
    HelX360NativeTextureLayout layout;
    if (!hel_x360_native_texture_layout(read_le32(colors + 8), expected_width, expected_height, &layout) ||
        read_le32(colors + 24) != layout.storage_width || read_le32(colors + 28) != layout.storage_height ||
        read_le32(colors + 32) != layout.row_pitch_bytes || read_le32(colors + 36) != layout.byte_length ||
        read_le32(colors + 40) != layout.swizzle || read_le32(colors + 44) != layout.signs ||
        read_le32(colors + 48) != layout.num_format || read_le32(colors + 52) != (uint32_t)layout.exp_adjust ||
        colors_length - HEL_X360_NATIVE_TEXTURE_HEADER_BYTES != layout.byte_length) return 0;
    *out = layout;
    *out_texels = colors + HEL_X360_NATIVE_TEXTURE_HEADER_BYTES;
    return 1;
}

int hel_x360_native_texture_fetch(const HelX360NativeTextureLayout *layout,
                                  uint32_t physical_address, uint32_t out_fetch[6]) {
    if (!out_fetch) return 0;
    for (uint32_t index = 0; index < 6; ++index) out_fetch[index] = 0;
    HelX360NativeTextureLayout canonical;
    if (!layout || !physical_address || physical_address >= 0x20000000u || (physical_address & 0xFFFu) ||
        !hel_x360_native_texture_layout(layout->format,layout->width,layout->height,&canonical) ||
        layout->storage_width != canonical.storage_width || layout->storage_height != canonical.storage_height ||
        layout->row_pitch_bytes != canonical.row_pitch_bytes || layout->byte_length != canonical.byte_length ||
        layout->swizzle != canonical.swizzle || layout->signs != canonical.signs ||
        layout->num_format != canonical.num_format || layout->exp_adjust != canonical.exp_adjust ||
        (uint64_t)physical_address + layout->byte_length > UINT64_C(0x20000000)) return 0;
    out_fetch[0] = XE_FETCH_TYPE_TEXTURE | (XE_TEX_CLAMP_TO_EDGE << 10) |
        (XE_TEX_CLAMP_TO_EDGE << 13) | (XE_TEX_CLAMP_TO_EDGE << 16) |
        ((layout->storage_width >> 5) << 22) | (layout->signs << 2);
    out_fetch[1] = layout->format | ((physical_address >> 12) << 12); /* endian none, linear, no mip chain */
    out_fetch[2] = (layout->width - 1u) | ((layout->height - 1u) << 13);
    out_fetch[3] = layout->num_format | (layout->swizzle << 1) | (((uint32_t)layout->exp_adjust & 63u) << 13) |
        (XE_TEX_FILTER_LINEAR << 19) | (XE_TEX_FILTER_LINEAR << 21) | (XE_TEX_FILTER_BASEMAP << 23);
    out_fetch[4] = 0;
    out_fetch[5] = XE_TEX_DIMENSION_2D << 9;
    return 1;
}
