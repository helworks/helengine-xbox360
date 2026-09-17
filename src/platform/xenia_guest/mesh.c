/*
 * Mesh storage and the unlit draw for the Xenia guest backend.
 *
 * hel_mesh_create validates the caller's data, then copies it into a
 * backend-owned physical allocation that lives until shutdown: vertices as
 * 7 big-endian floats (the GPU fetch constant selects k8in32), indices as
 * big-endian 16-bit values (VGT_DMA_SIZE selects k8in16). hel_draw_unlit
 * emits shaders, pipeline state, the MVP constants, the vertex fetch
 * constant and one indexed draw into the ring.
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"
#include "xenos_regs.h"

/* Scene models each take a slot, so the limit is a scene budget rather than
   a demo one. Each slot costs one 64 KiB physical allocation. */
#define HEL_MAX_MESHES 64u
#define HEL_MAX_VERTICES 4096u
#define HEL_MAX_INDICES 65535u
#define HEL_VERTEX_DWORDS 10u
#ifndef HEL_CULL_MODE
#define HEL_CULL_MODE (1u << 1) /* cull back faces, CCW front */
#endif

typedef struct {
    HelPhysBlock block;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t index_offset; /* bytes from block start */
} HelMeshSlot;

static HelMeshSlot g_meshes[HEL_MAX_MESHES];
static uint32_t g_mesh_count;
static uint32_t g_draw_count;

static uint32_t float_bits(float value) {
    union { float f; uint32_t u; } cast;
    cast.f = value;
    return cast.u;
}

int hel_mesh_create(const HelVertex *vertices, uint32_t vertex_count,
                    const uint16_t *indices, uint32_t index_count,
                    HelMesh *out_mesh) {
    if (out_mesh) {
        *out_mesh = 0;
    }
    if (!vertices || !indices || !out_mesh || vertex_count == 0 ||
        index_count == 0 || index_count % 3u != 0) {
        hel_log("HEL mesh: rejected (null data, empty, or index count not a "
                "multiple of three)");
        return HEL_ERR_MESH_INVALID;
    }
    if (vertex_count > HEL_MAX_VERTICES || index_count > HEL_MAX_INDICES) {
        hel_log_u32("HEL mesh: too large, vertices", vertex_count);
        hel_log_u32("HEL mesh: too large, indices", index_count);
        return HEL_ERR_MESH_LIMIT;
    }
    for (uint32_t i = 0; i < index_count; ++i) {
        if (indices[i] >= vertex_count) {
            hel_log_u32("HEL mesh: rejected index out of range at", i);
            hel_log_u32("HEL mesh: rejected index value", indices[i]);
            return HEL_ERR_MESH_INVALID;
        }
    }
    if (g_mesh_count >= HEL_MAX_MESHES) {
        hel_log("HEL mesh: mesh slots exhausted");
        return HEL_ERR_MESH_LIMIT;
    }
    HelMeshSlot *slot = &g_meshes[g_mesh_count];
    uint32_t vertex_bytes = vertex_count * HEL_VERTEX_DWORDS * 4u;
    uint32_t index_offset = (vertex_bytes + 255u) & ~255u;
    uint32_t total = index_offset + index_count * 2u;
    int err = hel_phys_alloc("mesh", (total + 0xFFFFu) & ~0xFFFFu, &slot->block);
    if (err) {
        return err;
    }
    volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)slot->block.virt;
    for (uint32_t v = 0; v < vertex_count; ++v) {
        const HelVertex *src = &vertices[v];
        dst[v * HEL_VERTEX_DWORDS + 0u] = float_bits(src->x);
        dst[v * HEL_VERTEX_DWORDS + 1u] = float_bits(src->y);
        dst[v * HEL_VERTEX_DWORDS + 2u] = float_bits(src->z);
        dst[v * HEL_VERTEX_DWORDS + 3u] = float_bits(src->nx);
        dst[v * HEL_VERTEX_DWORDS + 4u] = float_bits(src->ny);
        dst[v * HEL_VERTEX_DWORDS + 5u] = float_bits(src->nz);
        dst[v * HEL_VERTEX_DWORDS + 6u] = float_bits(src->r);
        dst[v * HEL_VERTEX_DWORDS + 7u] = float_bits(src->g);
        dst[v * HEL_VERTEX_DWORDS + 8u] = float_bits(src->b);
        dst[v * HEL_VERTEX_DWORDS + 9u] = float_bits(src->a);
    }
    volatile uint16_t *idx =
        (volatile uint16_t *)(uintptr_t)(slot->block.virt + index_offset);
    for (uint32_t i = 0; i < index_count; ++i) {
        idx[i] = indices[i];
    }
    slot->vertex_count = vertex_count;
    slot->index_count = index_count;
    slot->index_offset = index_offset;
    g_mesh_count++;
    *out_mesh = g_mesh_count; /* handles start at 1 */
    hel_log_u32("HEL mesh: created handle", *out_mesh);
    hel_log_u32("HEL mesh: vertices", vertex_count);
    hel_log_u32("HEL mesh: indices", index_count);
    hel_log_u32("HEL mesh: vertex data physical", slot->block.phys);
    hel_log_u32("HEL mesh: index data physical", slot->block.phys + index_offset);
    return HEL_OK;
}

static int draw(HelMesh mesh, const HelMat4 *mvp, const HelMat4 *model,
                const HelDrawParams *params);

int hel_draw_unlit(HelMesh mesh, const HelMat4 *mvp) {
    return draw(mesh, mvp, 0, 0);
}

int hel_draw_shaded(HelMesh mesh, const HelMat4 *mvp, const HelMat4 *model,
                    const HelDrawParams *params) {
    if (!model || !params) {
        return HEL_ERR_MESH_INVALID;
    }
    return draw(mesh, mvp, model, params);
}

/* One draw path for both shaders. The only differences are which vertex
   shader is emitted and how many constants follow the MVP, so splitting them
   into two functions would duplicate every piece of pipeline state and let
   the two drift. */
static int draw(HelMesh mesh, const HelMat4 *mvp, const HelMat4 *model,
                const HelDrawParams *params) {
    if (mesh == 0 || mesh > g_mesh_count || !mvp) {
        return HEL_ERR_MESH_INVALID;
    }
    int err = hel_shaders_validate();
    if (err) {
        return err;
    }
    const HelMeshSlot *slot = &g_meshes[mesh - 1u];
    uint32_t width = hel_video_width();
    uint32_t height = hel_video_height();
    const int shaded = (model != 0 && params != 0);

    hel_shaders_emit_kind(shaded ? HEL_SHADER_SHADED : HEL_SHADER_UNLIT);

    /* Viewport: NDC -> pixels, Direct3D 0..1 depth, clipping enabled. */
    hel_ring_put(XE_PACKET_TYPE0(XE_GPU_REG_PA_CL_VPORT_XSCALE, 6));
    hel_ring_put(float_bits((float)width * 0.5f));
    hel_ring_put(float_bits((float)width * 0.5f));
    hel_ring_put(float_bits(-(float)height * 0.5f));
    hel_ring_put(float_bits((float)height * 0.5f));
    hel_ring_put(float_bits(1.0f));
    hel_ring_put(float_bits(0.0f));
    /* All six viewport scale/offset enables plus VTX_W0_FMT (bit 10): the
       exported W is a true W, not 1/W. Without that bit the emulator applies
       a reciprocal and every position with W != 1 leaves the frustum. */
    hel_ring_reg(XE_GPU_REG_PA_CL_VTE_CNTL, 0x3Fu | (1u << 10));
    hel_ring_reg(XE_GPU_REG_PA_CL_CLIP_CNTL, 1u << 19); /* dx_clip_space_def */
    /* Back-face culling with counter-clockwise front faces (face bit = 0);
       the cube's fixed-angle captures verify the winding convention. */
    hel_ring_reg(XE_GPU_REG_PA_SU_SC_MODE_CNTL, HEL_CULL_MODE);
    hel_ring_reg(XE_GPU_REG_PA_SC_AA_CONFIG, 0u);
    hel_ring_reg(XE_GPU_REG_PA_SC_AA_MASK, 0xFFFFu);
    /* Depth test less, depth writes on; no stencil, no alpha test, no blend. */
    hel_ring_reg(XE_GPU_REG_RB_DEPTHCONTROL, (1u << 1) | (1u << 2) | (1u << 4));
    hel_ring_reg(XE_GPU_REG_RB_COLORCONTROL, 0u);
    hel_ring_reg(XE_GPU_REG_RB_BLENDCONTROL0, 0x00010001u); /* one/zero */
    hel_ring_reg(XE_GPU_REG_RB_COLOR_MASK, 0xFu);
    hel_ring_reg(XE_GPU_REG_RB_MODECONTROL, XE_EDRAM_MODE_COLOR_DEPTH);

    /* Float constants, starting at c0. The unlit shader reads only the MVP;
       the shaded one also reads the model transform, the material colour and
       the light, so the same packet just carries more of them. */
    const uint32_t constant_count = shaded ? 52u : 16u;
    hel_ring_type3(PM4_SET_CONSTANT, 1u + constant_count);
    hel_ring_put(0u); /* type 0 (ALU) << 16 | index 0 */
    for (uint32_t i = 0; i < 16u; ++i) {
        hel_ring_put(float_bits(mvp->m[i])); /* c0..c3 */
    }
    if (shaded) {
        for (uint32_t i = 0; i < 16u; ++i) {
            hel_ring_put(float_bits(model->m[i])); /* c4..c7 */
        }
        for (uint32_t i = 0; i < 4u; ++i) {
            hel_ring_put(float_bits(params->base_color[i])); /* c8 */
        }
        /* c9: the vector from the surface towards the light. */
        hel_ring_put(float_bits(params->light_direction[0]));
        hel_ring_put(float_bits(params->light_direction[1]));
        hel_ring_put(float_bits(params->light_direction[2]));
        hel_ring_put(float_bits(0.0f));
        /* c10: ambient, with 1 in w so the material's alpha survives the
           multiply chain rather than being scaled by the lighting. */
        hel_ring_put(float_bits(params->ambient[0]));
        hel_ring_put(float_bits(params->ambient[1]));
        hel_ring_put(float_bits(params->ambient[2]));
        hel_ring_put(float_bits(1.0f));
        /* c11: light colour scaled by intensity, with 0 in w for the same
           reason. */
        hel_ring_put(float_bits(params->light_color[0]));
        hel_ring_put(float_bits(params->light_color[1]));
        hel_ring_put(float_bits(params->light_color[2]));
        hel_ring_put(float_bits(0.0f));
        /* c12: zero, which is what the Lambert term is clamped against. */
        for (uint32_t i = 0; i < 4u; ++i) {
            hel_ring_put(float_bits(0.0f));
        }
    }

    /* Vertex fetch constant 0 -> the mesh's vertex data. */
    hel_ring_put(XE_PACKET_TYPE0(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 2));
    hel_ring_put(3u | (slot->block.phys & ~3u));
    hel_ring_put(XE_ENDIAN_8IN32 |
                 ((slot->vertex_count * HEL_VERTEX_DWORDS) << 2));

    /* Indexed triangle list from the mesh's 16-bit big-endian indices. */
    hel_ring_type3(PM4_DRAW_INDX, 4);
    hel_ring_put(0u);
    hel_ring_put(XE_DRAW_INITIATOR(XE_PRIM_TRIANGLE_LIST, XE_SOURCE_SELECT_DMA,
                                   slot->index_count));
    hel_ring_put(slot->block.phys + slot->index_offset);
    hel_ring_put(slot->index_count | (XE_ENDIAN_8IN16 << 30));

    if (g_draw_count == 0) {
        hel_log_u32("HEL draw: first draw, mesh", mesh);
        hel_log_u32("HEL draw: indices", slot->index_count);
        hel_log_u32("HEL draw: vertices", slot->vertex_count);
    }
    g_draw_count++;
    return hel_ring_overflowed() ? HEL_ERR_RING_FULL : HEL_OK;
}

void hel_mesh_release_all(void) {
    for (uint32_t i = 0; i < g_mesh_count; ++i) {
        hel_phys_free(&g_meshes[i].block);
    }
    g_mesh_count = 0;
}
