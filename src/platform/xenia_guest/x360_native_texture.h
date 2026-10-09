/* Validated single-level, linear Xenos texture storage shared by the uploader and host tests. */
#ifndef HEL_X360_NATIVE_TEXTURE_H
#define HEL_X360_NATIVE_TEXTURE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HEL_X360_NATIVE_TEXTURE_HEADER_BYTES 56u
#define HEL_X360_NATIVE_TEXTURE_FORMAT_COUNT 44u
#define HEL_X360_NATIVE_COLOR_FORMAT 14u

/* GPU format traits and canonical sampler interpretation; unsupported hardware enums are not uploadable. */
typedef struct {
    uint32_t code, block_width, block_height, bytes_per_block, swizzle;
} HelX360NativeTextureFormat;

/* Storage describes padded block rows; width and height remain the real normalized-coordinate image extent. */
typedef struct {
    uint32_t format, width, height, storage_width, storage_height;
    uint32_t row_pitch_bytes, byte_length, swizzle, signs, num_format;
    int32_t exp_adjust;
} HelX360NativeTextureLayout;

/* Looks up only the 44 formats with defined encoding, without reinterpreting unsupported formats. */
int hel_x360_native_texture_format(uint32_t code, HelX360NativeTextureFormat *out);
/* Computes the smallest pitch aligned to 32 blocks and 256 bytes, within the 8192-pixel image limit. */
int hel_x360_native_texture_layout(uint32_t code, uint32_t width, uint32_t height,
                                   HelX360NativeTextureLayout *out);
/* Requires an exact X3T1 v1 header and texel length; returns a borrowed span for the immediate physical copy. */
int hel_x360_native_texture_parse(const uint8_t *colors, uint32_t colors_length,
    uint32_t expected_width, uint32_t expected_height,
    HelX360NativeTextureLayout *out, const uint8_t **out_texels);
/* Revalidates the canonical layout and physical base, then emits all six endian-none 2D fetch words. */
int hel_x360_native_texture_fetch(const HelX360NativeTextureLayout *layout,
                                  uint32_t physical_address, uint32_t out_fetch[6]);
/* Anonymous legacy RGBA/Opaque inline atlases carry font coverage that must bypass ordinary opaque alpha conversion. */
int hel_x360_preserve_anonymous_font_alpha(uint32_t color_format, uint32_t alpha_precision, int has_stable_identity);
#ifdef __cplusplus
}
#endif
#endif
