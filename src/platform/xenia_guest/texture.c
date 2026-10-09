/*
 * Texture storage for the Xenia guest backend.
 *
 * A texture is uploaded once into a backend-owned physical allocation that
 * lives until its owner retires it and the GPU fence completes, and is described by a texture fetch
 * constant written into the ring before a draw that samples it.
 *
 * Legacy RGBA uploads are padded here. Native X3T1 uploads already carry the
 * exact linear/block-row storage and canonical sampler interpretation; those
 * bytes are copied directly, without expanding compressed or packed texels.
 * Two hardware requirements shape the copy, and both are silent if broken:
 *
 *   Row alignment. A linear texture's rows must start on a 256-byte boundary,
 *   and the pitch field of the fetch constant is expressed in pixels shifted
 *   right by five. With four bytes a pixel, both are satisfied by padding each
 *   row out to a multiple of 64 pixels. A texture whose width is not a
 *   multiple of 64 is therefore wider in memory than on screen, and the pad
 *   is never sampled because the size field still says the real width.
 *
 *   Byte order. The 32-bit format is a straight byte copy on the host side,
 *   red, green, blue then alpha at increasing addresses, with an identity
 *   swizzle. That is what the emulator's own format table says it loads. So
 *   the endian field is set to none: there is no multi-byte scalar here to
 *   swap, and swapping would reverse each texel into alpha, blue, green, red.
 *
 * Field layouts follow xenia-canary src/xenia/gpu/xenos.h,
 * xe_gpu_texture_fetch_t.
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"
#include "xenos_regs.h"
#include "x360_native_texture.h"

/* One allocation per texture, as meshes do, so the ceiling is a scene budget.
   Interface textures are a font atlas and a handful of sprites. */
#define HEL_MAX_TEXTURES 32u
#define HEL_TEXTURE_MAX_DIMENSION 8192u
/* Four bytes a pixel, rows padded to this many pixels; see the note above. */
#define HEL_TEXTURE_ROW_PIXELS 64u

typedef struct {
    HelPhysBlock block;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    /* Complete immutable fetch descriptor for this allocation, including native format and swizzle. */
    uint32_t fetch[6];
    /* Retired allocations remain readable until the submitted frame completes. */
    uint32_t retired;
} HelTextureSlot;

static HelTextureSlot g_textures[HEL_MAX_TEXTURES];
static uint32_t g_texture_count;

uint32_t hel_texture_count(void) {
    uint32_t live = 0;
    for (uint32_t index = 0; index < g_texture_count; ++index) {
        if (g_textures[index].block.virt && !g_textures[index].retired) ++live;
    }
    return live;
}

int hel_texture_create(const uint8_t *rgba, uint32_t width, uint32_t height,
                       HelTexture *out_texture) {
    if (!rgba || !out_texture || width == 0 || height == 0 ||
        width > HEL_TEXTURE_MAX_DIMENSION ||
        height > HEL_TEXTURE_MAX_DIMENSION) {
        return HEL_ERR_MESH_INVALID;
    }
    uint32_t slot_index = 0;
    while (slot_index < g_texture_count && g_textures[slot_index].block.virt) ++slot_index;
    if (slot_index >= HEL_MAX_TEXTURES) {
        return HEL_ERR_MESH_LIMIT;
    }

    HelTextureSlot *slot = &g_textures[slot_index];
    const uint32_t pitch_pixels =
        (width + HEL_TEXTURE_ROW_PIXELS - 1u) & ~(HEL_TEXTURE_ROW_PIXELS - 1u);
    const uint32_t bytes = pitch_pixels * height * 4u;
    int err = hel_phys_alloc("texture", (bytes + 0xFFFFu) & ~0xFFFFu,
                             &slot->block);
    if (err) {
        return err;
    }

    volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)slot->block.virt;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *source = rgba + (uintptr_t)y * width * 4u;
        volatile uint8_t *row = dst + (uintptr_t)y * pitch_pixels * 4u;
        uint32_t x = 0;
        for (; x < width * 4u; ++x) {
            row[x] = source[x];
        }
        /* The pad is never sampled, but it is cleared rather than left as
           whatever the allocation held, so a wrong pitch shows as black
           rather than as somebody else's pixels. */
        for (; x < pitch_pixels * 4u; ++x) {
            row[x] = 0u;
        }
    }

    slot->width = width;
    slot->height = height;
    slot->pitch_pixels = pitch_pixels;
    slot->retired = 0;
    HelX360NativeTextureLayout layout;
    if (!hel_x360_native_texture_layout(XE_TEX_FORMAT_8_8_8_8, width, height, &layout) ||
        !hel_x360_native_texture_fetch(&layout,slot->block.phys,slot->fetch)) {
        hel_phys_free(&slot->block);
        return HEL_ERR_MESH_INVALID;
    }
    if (slot_index == g_texture_count) ++g_texture_count;
    *out_texture = slot_index + 1u; /* handles start at 1; fenced slots can be reused */
    hel_log_u32("HEL texture: created handle", *out_texture);
    hel_log_u32("HEL texture: width", width);
    hel_log_u32("HEL texture: height", height);
    hel_log_u32("HEL texture: row pitch pixels", pitch_pixels);
    hel_log_u32("HEL texture: physical address", slot->block.phys);
    /* The first and a middle texel, as they now sit in memory, so a texture
       that samples black can be told apart from one that was uploaded black.
       Read back through the same pointer that wrote them. */
    {
        const volatile uint8_t *first = dst;
        const volatile uint8_t *middle =
            dst + ((uintptr_t)(height / 2u) * pitch_pixels + width / 2u) * 4u;
        hel_log_u32("HEL texture: first texel RGBA",
                    ((uint32_t)first[0] << 24) | ((uint32_t)first[1] << 16) |
                        ((uint32_t)first[2] << 8) | (uint32_t)first[3]);
        hel_log_u32("HEL texture: middle texel RGBA",
                    ((uint32_t)middle[0] << 24) | ((uint32_t)middle[1] << 16) |
                        ((uint32_t)middle[2] << 8) | (uint32_t)middle[3]);
    }
    return HEL_OK;
}

int hel_texture_create_native(const uint8_t *colors, uint32_t colors_length,
    uint32_t width, uint32_t height, HelTexture *out_texture) {
    if (!out_texture) return HEL_ERR_MESH_INVALID;
    *out_texture = 0;
    HelX360NativeTextureLayout layout;
    const uint8_t *texels = 0;
    if (!hel_x360_native_texture_parse(colors,colors_length,width,height,&layout,&texels))
        return HEL_ERR_MESH_INVALID;
    uint32_t slot_index = 0;
    while (slot_index < g_texture_count && g_textures[slot_index].block.virt) ++slot_index;
    if (slot_index >= HEL_MAX_TEXTURES) return HEL_ERR_MESH_LIMIT;
    HelTextureSlot *slot = &g_textures[slot_index];
    if (layout.byte_length > UINT32_MAX - 0xFFFFu) return HEL_ERR_MESH_INVALID;
    const int error = hel_phys_alloc("native texture",(layout.byte_length + 0xFFFFu) & ~0xFFFFu,&slot->block);
    if (error) return error;
    if (!hel_x360_native_texture_fetch(&layout,slot->block.phys,slot->fetch)) {
        hel_phys_free(&slot->block);
        return HEL_ERR_MESH_INVALID;
    }
    volatile uint8_t *destination = (volatile uint8_t *)(uintptr_t)slot->block.virt;
    for (uint32_t index = 0; index < layout.byte_length; ++index) destination[index] = texels[index];
    slot->width = width;
    slot->height = height;
    slot->pitch_pixels = layout.storage_width;
    slot->retired = 0;
    if (slot_index == g_texture_count) ++g_texture_count;
    *out_texture = slot_index + 1u;
    hel_log_u32("HEL texture: native handle",*out_texture);
    hel_log_u32("HEL texture: native hardware format",layout.format);
    hel_log_u32("HEL texture: native copied bytes",layout.byte_length);
    hel_log_u32("HEL texture: native row pitch bytes",layout.row_pitch_bytes);
    return HEL_OK;
}

void hel_texture_bind(HelTexture texture) {
    if (texture == 0 || texture > g_texture_count) {
        return;
    }
    const HelTextureSlot *slot = &g_textures[texture - 1u];
    if (!slot->block.virt || slot->retired) return;

    /* Sizes are stored with one subtracted from each component, and the base
       address is shifted right by twelve, which is why a page-aligned
       allocation is required rather than merely convenient. */

    /* Texture fetch constant 1. Constant 0's six dwords are the same storage
       as vertex fetch constants 0 to 2, and the mesh draw writes vertex
       constant 0, so a texture there would overwrite the vertex stream's own
       description. */
    hel_ring_put(XE_PACKET_TYPE0(
        XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + XE_TEXTURE_FETCH_DWORDS, 6));
    for (uint32_t i = 0; i < 6u; ++i) {
        hel_ring_put(slot->fetch[i]);
    }
}

void hel_texture_release(HelTexture texture) {
    if (texture == 0 || texture > g_texture_count) return;
    HelTextureSlot *slot = &g_textures[texture - 1u];
    if (!slot->block.virt || slot->retired) return;
    slot->retired = 1;
    hel_log_u32("HEL texture: retired handle", texture);
}

void hel_texture_collect_retired(void) {
    for (uint32_t i = 0; i < g_texture_count; ++i) {
        if (!g_textures[i].block.virt || !g_textures[i].retired) continue;
        hel_phys_free(&g_textures[i].block);
        g_textures[i].retired = 0;
        g_textures[i].width = g_textures[i].height = g_textures[i].pitch_pixels = 0;
        hel_log_u32("HEL texture: fenced release handle", i + 1u);
    }
}

void hel_texture_release_all(void) {
    for (uint32_t i = 0; i < g_texture_count; ++i) {
        if (g_textures[i].block.virt) hel_phys_free(&g_textures[i].block);
        g_textures[i].retired = 0;
        g_textures[i].width = g_textures[i].height = g_textures[i].pitch_pixels = 0;
    }
    g_texture_count = 0;
}
