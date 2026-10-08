/*
 * Mesh storage and the unlit draw for the Xenia guest backend.
 *
 * hel_mesh_create validates the caller's data, then copies it into a
 * backend-owned physical allocation retired by its model owner after a GPU fence: vertices as
 * 7 big-endian floats (the GPU fetch constant selects k8in32), indices as
 * big-endian 16-bit values (VGT_DMA_SIZE selects k8in16). hel_draw_unlit
 * emits shaders, pipeline state, the MVP constants, the vertex fetch
 * constant and one indexed draw into the ring.
 */
#include <stdint.h>
#include <string.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"
#include "xenos_regs.h"

/* Scene models each take a slot, so the limit is a scene budget rather than
   a demo one. Each slot costs one 64 KiB physical allocation. */
#define HEL_MAX_MESHES 64u
#define HEL_MAX_VERTICES 4096u
#define HEL_MAX_INDICES 65535u
/* Shared sprites and outlined text use 96-byte vertices plus six 16-bit
   indices per quad. Reserve a bounded 1 MiB per frame, about 2600 quads,
   so a screen of outlined menu labels fits together with its sprites. */
#define HEL_UI_ARENA_BYTES 0x100000u
#define HEL_VERTEX_DWORDS 12u
#ifndef HEL_CULL_MODE
#define HEL_CULL_MODE (1u << 1) /* cull back faces, CCW front */
#endif

typedef struct {
    HelPhysBlock block;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t index_offset; /* bytes from block start */
    /* Memory remains valid until present acknowledges the last submitted draw. */
    uint32_t retired;
} HelMeshSlot;

static HelMeshSlot g_meshes[HEL_MAX_MESHES];
static uint32_t g_mesh_count;
static uint32_t g_draw_count;
/// Validated per-drawable UI clip, independent of scene depth-tested geometry.
static uint32_t g_ui_scissor[4];
/// False until the current camera resolves a drawable's authored clip chain.
static int g_ui_scissor_active;

/* Interface geometry is rebuilt every frame: a panel moves, resizes and comes
   and goes, so it cannot own a mesh slot. Each slot costs a 64 KiB physical
   allocation that is never released, and there are only 64, so a few seconds
   of interface would exhaust them.
 *
 * Instead one arena is filled from the start each frame. That is safe because
 * hel_frame_present blocks until the fence retires, so the GPU has finished
 * reading a frame's geometry before the next frame overwrites it. If frames
 * are ever allowed to overlap, this needs one arena per frame in flight. */
/* The texture the next interface draw samples, or zero for none. It is set
   immediately before the draw and cleared by it, rather than being a mode the
   caller has to remember to turn off. */
static HelTexture g_pending_texture;
static int g_reported_ui_draw;
static HelPhysBlock g_ui_arena;
static uint32_t g_ui_used;
static uint32_t g_ui_peak;
static int g_ui_reported_overflow;

static uint32_t float_bits(float value) {
    union { float f; uint32_t u; } cast;
    cast.f = value;
    return cast.u;
}

int hel_ui_arena_init(void) {
    if (g_ui_arena.virt != 0) {
        return HEL_OK;
    }
    return hel_phys_alloc("ui", HEL_UI_ARENA_BYTES, &g_ui_arena);
}

void hel_ui_frame_reset(void) { g_ui_used = 0; g_ui_scissor_active = 0; }

/// Validates screen bounds before publishing the clip used by native rounded rectangles.
int hel_ui_set_scissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    const uint32_t target_width = hel_video_width(), target_height = hel_video_height();
    if (x > target_width || y > target_height || width > target_width - x || height > target_height - y)
        return HEL_ERR_MESH_INVALID;
    g_ui_scissor[0] = x; g_ui_scissor[1] = y;
    g_ui_scissor[2] = width; g_ui_scissor[3] = height;
    g_ui_scissor_active = 1;
    return HEL_OK;
}

uint32_t hel_ui_peak_bytes(void) { return g_ui_peak; }

/* Copies one batch into the arena and describes it the way a mesh slot does,
   so the shared draw path does not care where geometry came from. */
static int ui_arena_write(const HelVertex *vertices, uint32_t vertex_count,
                          const uint16_t *indices, uint32_t index_count,
                          HelMeshSlot *out_slot) {
    if (!vertices || !indices || !out_slot || vertex_count == 0 ||
        index_count == 0 || vertex_count > HEL_MAX_VERTICES ||
        index_count > HEL_MAX_INDICES) {
        return HEL_ERR_MESH_INVALID;
    }
    if (g_ui_arena.virt == 0) {
        return HEL_ERR_VIDEO_NOT_READY;
    }
    for (uint32_t i = 0; i < index_count; ++i) {
        if (indices[i] >= vertex_count) {
            return HEL_ERR_MESH_INVALID;
        }
    }

    /* The fetch constant addresses vertices from the block base, so each batch
       starts on a four-byte boundary and its indices after them. */
    const uint32_t vertex_bytes = vertex_count * HEL_VERTEX_DWORDS * 4u;
    const uint32_t index_offset = (g_ui_used + vertex_bytes + 3u) & ~3u;
    const uint32_t end = index_offset + index_count * 2u;
    if (end > HEL_UI_ARENA_BYTES) {
        if (!g_ui_reported_overflow) {
            g_ui_reported_overflow = 1;
            hel_log_u32("HEL ui: arena full, batch dropped at byte", g_ui_used);
        }
        return HEL_ERR_MESH_LIMIT;
    }

    volatile uint32_t *dst =
        (volatile uint32_t *)(uintptr_t)(g_ui_arena.virt + g_ui_used);
    for (uint32_t v = 0; v < vertex_count; ++v) {
        const HelVertex *src = &vertices[v];
        dst[v * HEL_VERTEX_DWORDS + 0u] = float_bits(src->x);
        dst[v * HEL_VERTEX_DWORDS + 1u] = float_bits(src->y);
        dst[v * HEL_VERTEX_DWORDS + 2u] = float_bits(src->z);
        dst[v * HEL_VERTEX_DWORDS + 3u] = float_bits(src->nx);
        dst[v * HEL_VERTEX_DWORDS + 4u] = float_bits(src->ny);
        dst[v * HEL_VERTEX_DWORDS + 5u] = float_bits(src->nz);
        dst[v * HEL_VERTEX_DWORDS + 6u] = float_bits(src->u);
        dst[v * HEL_VERTEX_DWORDS + 7u] = float_bits(src->v);
        dst[v * HEL_VERTEX_DWORDS + 8u] = float_bits(src->r);
        dst[v * HEL_VERTEX_DWORDS + 9u] = float_bits(src->g);
        dst[v * HEL_VERTEX_DWORDS + 10u] = float_bits(src->b);
        dst[v * HEL_VERTEX_DWORDS + 11u] = float_bits(src->a);
    }
    volatile uint16_t *idx =
        (volatile uint16_t *)(uintptr_t)(g_ui_arena.virt + index_offset);
    for (uint32_t i = 0; i < index_count; ++i) {
        idx[i] = indices[i];
    }

    out_slot->block.virt = g_ui_arena.virt + g_ui_used;
    out_slot->block.phys = g_ui_arena.phys + g_ui_used;
    out_slot->block.size = end - g_ui_used;
    out_slot->vertex_count = vertex_count;
    out_slot->index_count = index_count;
    out_slot->index_offset = index_offset - g_ui_used;
    g_ui_used = (end + 3u) & ~3u;
    if (g_ui_used > g_ui_peak) {
        g_ui_peak = g_ui_used;
    }
    return HEL_OK;
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
    uint32_t slot_index = 0;
    while (slot_index < g_mesh_count && g_meshes[slot_index].block.virt) ++slot_index;
    if (slot_index >= HEL_MAX_MESHES) {
        hel_log("HEL mesh: mesh slots exhausted");
        return HEL_ERR_MESH_LIMIT;
    }
    HelMeshSlot *slot = &g_meshes[slot_index];
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
        dst[v * HEL_VERTEX_DWORDS + 6u] = float_bits(src->u);
        dst[v * HEL_VERTEX_DWORDS + 7u] = float_bits(src->v);
        dst[v * HEL_VERTEX_DWORDS + 8u] = float_bits(src->r);
        dst[v * HEL_VERTEX_DWORDS + 9u] = float_bits(src->g);
        dst[v * HEL_VERTEX_DWORDS + 10u] = float_bits(src->b);
        dst[v * HEL_VERTEX_DWORDS + 11u] = float_bits(src->a);
    }
    volatile uint16_t *idx =
        (volatile uint16_t *)(uintptr_t)(slot->block.virt + index_offset);
    for (uint32_t i = 0; i < index_count; ++i) {
        idx[i] = indices[i];
    }
    slot->vertex_count = vertex_count;
    slot->index_count = index_count;
    slot->index_offset = index_offset;
    slot->retired = 0;
    if (slot_index == g_mesh_count) ++g_mesh_count;
    *out_mesh = slot_index + 1u; /* only fenced slots can be reused */
    hel_log_u32("HEL mesh: created handle", *out_mesh);
    hel_log_u32("HEL mesh: vertices", vertex_count);
    hel_log_u32("HEL mesh: indices", index_count);
    hel_log_u32("HEL mesh: vertex data physical", slot->block.phys);
    hel_log_u32("HEL mesh: index data physical", slot->block.phys + index_offset);
    return HEL_OK;
}

static int draw(const HelMeshSlot *slot, const HelMat4 *mvp,
                const HelMat4 *model, const HelDrawParams *params,
                int depth_tested, int blended, const HelBatchDraw *batch);

int hel_draw_unlit(HelMesh mesh, const HelMat4 *mvp) {
    if (mesh == 0 || mesh > g_mesh_count || !g_meshes[mesh - 1u].block.virt || g_meshes[mesh - 1u].retired) {
        return HEL_ERR_MESH_INVALID;
    }
    return draw(&g_meshes[mesh - 1u], mvp, 0, 0, 1, 0, 0);
}

int hel_draw_shaded(HelMesh mesh, const HelMat4 *mvp, const HelMat4 *model,
                    const HelDrawParams *params) {
    if (mesh == 0 || mesh > g_mesh_count || !g_meshes[mesh - 1u].block.virt || g_meshes[mesh - 1u].retired || !model || !params) {
        return HEL_ERR_MESH_INVALID;
    }
    return draw(&g_meshes[mesh - 1u], mvp, model, params, 1, 0, 0);
}

int hel_draw_ui(const HelVertex *vertices, uint32_t vertex_count,
                const uint16_t *indices, uint32_t index_count,
                const HelMat4 *transform, const HelDrawParams *params) {
    return hel_draw_ui_textured(vertices, vertex_count, indices, index_count,
                                transform, params, 0);
}

int hel_draw_ui_textured(const HelVertex *vertices, uint32_t vertex_count,
                         const uint16_t *indices, uint32_t index_count,
                         const HelMat4 *transform, const HelDrawParams *params,
                         HelTexture texture) {
    HelMeshSlot slot;
    int err = ui_arena_write(vertices, vertex_count, indices, index_count,
                             &slot);
    if (err) {
        return err;
    }
    g_pending_texture = texture;
    /* Interface geometry is submitted in the order the queues hand it over and
       composes over the scene, so it neither tests nor writes depth. Testing
       it against a scene that was already resolved would hide the interface
       behind whatever happened to be in front. */
    /* Blended, because an interface is authored with translucency and a
       panel drawn opaque hides the scene behind it. Batches are submitted in
       queue order and do not write depth, so they compose in that order. */
    return draw(&slot, transform, transform, params, 0, 1, 0);
}

static void batch_ring_word(void *context, uint32_t word) {
    (void)context;
    hel_ring_put(word);
}

int hel_draw_mesh_program(HelMesh mesh, const HelShaderProgram *program,
    const float row_mvp[16], const uint32_t color[4]) {
    uint32_t constants[16];
    if (!mesh || mesh > g_mesh_count || !g_meshes[mesh - 1u].block.virt || g_meshes[mesh - 1u].retired || !color || hel_shader_mesh_constants(program, row_mvp, constants))
        return HEL_ERR_MESH_INVALID;
    if (hel_ring_available() < program->view.pair_upload_dwords + 128u) return HEL_ERR_RING_FULL;
    HelBatchDraw context = {0};
    context.program = program;
    context.constants = constants;
    context.pixel_constants = color;
    context.viewport[2] = context.scissor[2] = hel_video_width();
    context.viewport[3] = context.scissor[3] = hel_video_height();
    HelMat4 unused = {{0}};
    g_pending_texture = 0;
    return draw(&g_meshes[mesh - 1u], &unused, 0, 0, 1, 0, &context);
}

int hel_draw_batch(const void *vertices, uint32_t vertex_count,
    const uint16_t *indices, uint32_t index_count, HelTexture texture,
    const HelBatchDraw *batch) {
    if (!vertices || !indices || !batch || !batch->program || !batch->constants ||
        batch->program->profile != 2 || !batch->program->view.data ||
        batch->program->constant_words[0] != 16 || batch->program->constant_words[1] != 0 ||
        !texture || !vertex_count || vertex_count > HEL_MAX_VERTICES ||
        !index_count || index_count > HEL_MAX_INDICES || index_count % 3) return HEL_ERR_MESH_INVALID;
    uint32_t width = hel_video_width(), height = hel_video_height();
    if (!batch->viewport[2] || !batch->viewport[3] ||
        batch->viewport[0] > width || batch->viewport[2] > width - batch->viewport[0] ||
        batch->viewport[1] > height || batch->viewport[3] > height - batch->viewport[1] ||
        batch->scissor[0] > width || batch->scissor[2] > width - batch->scissor[0] ||
        batch->scissor[1] > height || batch->scissor[3] > height - batch->scissor[1]) return HEL_ERR_MESH_INVALID;
    if (!batch->scissor[2] || !batch->scissor[3]) return HEL_OK;
    if (hel_ring_available() < batch->program->view.pair_upload_dwords + 128u) return HEL_ERR_RING_FULL;
    if (!g_ui_arena.virt) return HEL_ERR_VIDEO_NOT_READY;
    for (uint32_t i = 0; i < index_count; ++i) if (indices[i] >= vertex_count) return HEL_ERR_MESH_INVALID;
    uint32_t vertex_bytes = vertex_count * 96u;
    uint32_t index_offset = g_ui_used + vertex_bytes;
    uint32_t end = (index_offset + index_count * 2u + 3u) & ~3u;
    if (end > HEL_UI_ARENA_BYTES) {
        hel_log_u32("HEL batch: required arena bytes", end);
        hel_log_u32("HEL batch: arena capacity bytes", HEL_UI_ARENA_BYTES);
        return HEL_ERR_MESH_LIMIT;
    }
    volatile uint32_t *destination = (volatile uint32_t *)(uintptr_t)(g_ui_arena.virt + g_ui_used);
    const uint8_t *source = (const uint8_t *)vertices;
    for (uint32_t i = 0; i < vertex_count * 24u; ++i) {
        uint32_t word;
        memcpy(&word, source + i * 4u, 4);
        destination[i] = word;
    }
    volatile uint16_t *index_destination = (volatile uint16_t *)(uintptr_t)(g_ui_arena.virt + index_offset);
    for (uint32_t i = 0; i < index_count; ++i) index_destination[i] = indices[i];
    HelMeshSlot slot = {{g_ui_arena.virt + g_ui_used, g_ui_arena.phys + g_ui_used, end - g_ui_used},
        vertex_count, index_count, index_offset - g_ui_used, 0};
    g_ui_used = end;
    if (end > g_ui_peak) g_ui_peak = end;
    HelMat4 unused = {{0}};
    g_pending_texture = texture;
    return draw(&slot, &unused, 0, 0, 0, 1, batch);
}

/* One draw path for every shader and both geometry sources. The differences
   are which vertex shader is emitted, how many constants follow, and whether
   depth applies; splitting them would duplicate every piece of pipeline state
   and let the copies drift. */
static int draw(const HelMeshSlot *slot, const HelMat4 *mvp,
                const HelMat4 *model, const HelDrawParams *params,
                int depth_tested, int blended, const HelBatchDraw *batch) {
#if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE
    const int gpu_probe = hel_shader_gpu_probe_is_active();
#endif
    if (!slot || !mvp) {
#if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE
        if (gpu_probe) hel_shader_gpu_probe_set_active(0);
#endif
        return HEL_ERR_MESH_INVALID;
    }
#if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE
    int err = HEL_OK;
    if (!gpu_probe && !batch) {
        err = hel_shaders_validate();
        if (err) {
            return err;
        }
    }
#else
    int err = batch ? HEL_OK : hel_shaders_validate();
    if (err) {
        return err;
    }
#endif
    uint32_t width = hel_video_width();
    uint32_t height = hel_video_height();
    const int shaded = (model != 0 && params != 0);

    if (batch) {
        err = hel_shader_program_emit(batch->program, batch->constants, 16,
            batch->program->profile == 1 ? batch->pixel_constants : 0, batch->program->profile == 1 ? 4 : 0,
            hel_ring_available(), batch_ring_word, 0);
        if (err) return HEL_ERR_SHADER_INVALID;
        hel_shaders_frame_reset();
    }

    const HelTexture texture = g_pending_texture;
    g_pending_texture = 0;
    if (texture != 0 && !g_reported_ui_draw) {
        g_reported_ui_draw = 1;
        hel_log_u32("HEL draw: first textured draw, handle", texture);
        hel_log_u32("HEL draw: first textured draw, vertices",
                    slot->vertex_count);
    }
#if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE
    if (gpu_probe) {
        err = hel_shader_gpu_probe_emit();
        if (!err) {
            hel_shader_gpu_probe_constants();
            if (hel_ring_overflowed()) {
                err = HEL_ERR_RING_FULL;
            }
        }
        /* No failure path below may leave the override armed for another draw. */
        hel_shader_gpu_probe_set_active(0);
        if (err) {
            return err;
        }
    } else
#endif
    {
        if (!batch) hel_shaders_emit_kind(texture != 0   ? HEL_SHADER_TEXTURED
                              : shaded       ? HEL_SHADER_SHADED
                                             : HEL_SHADER_UNLIT);
    }

    /* Viewport: NDC -> pixels, Direct3D 0..1 depth, clipping enabled. */
    hel_ring_put(XE_PACKET_TYPE0(XE_GPU_REG_PA_CL_VPORT_XSCALE, 6));
    hel_ring_put(float_bits((float)(batch ? batch->viewport[2] : width) * 0.5f));
    hel_ring_put(float_bits(batch ? (float)batch->viewport[0] + (float)batch->viewport[2] * 0.5f : (float)width * 0.5f));
    hel_ring_put(float_bits(-(float)(batch ? batch->viewport[3] : height) * 0.5f));
    hel_ring_put(float_bits(batch ? (float)batch->viewport[1] + (float)batch->viewport[3] * 0.5f : (float)height * 0.5f));
    hel_ring_put(float_bits(1.0f));
    hel_ring_put(float_bits(0.0f));
    /* All six viewport scale/offset enables plus VTX_W0_FMT (bit 10): the
       exported W is a true W, not 1/W. Without that bit the emulator applies
       a reciprocal and every position with W != 1 leaves the frustum. */
    hel_ring_reg(XE_GPU_REG_PA_CL_VTE_CNTL, 0x3Fu | (1u << 10));
    hel_ring_reg(XE_GPU_REG_PA_CL_CLIP_CNTL, 1u << 19); /* dx_clip_space_def */
    /* Back-face culling with counter-clockwise front faces (face bit = 0);
       the cube's fixed-angle captures verify the winding convention. */
    /* Interface quads are built here and face the viewer by construction, but
       a negative scale or a rotation past 180 degrees flips their winding, so
       they are not culled. Scene geometry keeps back-face culling. */
    hel_ring_reg(XE_GPU_REG_PA_SU_SC_MODE_CNTL,
                 depth_tested ? HEL_CULL_MODE : 0u);
    hel_ring_reg(XE_GPU_REG_PA_SC_AA_CONFIG, 0u);
    hel_ring_reg(XE_GPU_REG_PA_SC_AA_MASK, 0xFFFFu);
    /* Depth test less and depth writes on for the scene; both off for
       interface geometry. No stencil, no alpha test, no blend either way. */
    hel_ring_reg(XE_GPU_REG_RB_DEPTHCONTROL,
                 depth_tested ? ((1u << 1) | (1u << 2) | (1u << 4)) : 0u);
    hel_ring_reg(XE_GPU_REG_RB_COLORCONTROL, 0u);
    hel_ring_reg(XE_GPU_REG_RB_BLENDCONTROL0,
                 blended ? XE_BLEND_ALPHA_OVER : XE_BLEND_OPAQUE);
    hel_ring_reg(XE_GPU_REG_RB_COLOR_MASK, 0xFu);
    hel_ring_reg(XE_GPU_REG_RB_MODECONTROL, XE_EDRAM_MODE_COLOR_DEPTH);
    const uint32_t* scissor = batch ? batch->scissor :
        (!depth_tested && g_ui_scissor_active ? g_ui_scissor : 0);
    hel_ring_reg(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, (1u << 31) |
        (scissor ? scissor[0] | (scissor[1] << 16) : 0));
    hel_ring_reg(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, scissor ?
        (scissor[0] + scissor[2]) | ((scissor[1] + scissor[3]) << 16) : width | (height << 16));

    /* Float constants, starting at c0. The unlit shader reads only the MVP;
       the shaded one also reads the model transform, the material colour and
       the light, so the same packet just carries more of them. */
#if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE
    if (!gpu_probe) {
#endif
    if (!batch) {
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
#if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE
    }
#endif
    }

    if (texture != 0) {
        hel_texture_bind(texture);
    }

    /* Vertex fetch constant 0 -> the mesh's vertex data. */
    hel_ring_put(XE_PACKET_TYPE0(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 2));
    hel_ring_put(3u | (slot->block.phys & ~3u));
    hel_ring_put(XE_ENDIAN_8IN32 |
                 ((slot->vertex_count * (batch && batch->program->profile == 2 ? 24u : HEL_VERTEX_DWORDS)) << 2));

    /* Indexed triangle list from the mesh's 16-bit big-endian indices. */
    hel_ring_type3(PM4_DRAW_INDX, 4);
    hel_ring_put(0u);
    hel_ring_put(XE_DRAW_INITIATOR(XE_PRIM_TRIANGLE_LIST, XE_SOURCE_SELECT_DMA,
                                   slot->index_count));
    hel_ring_put(slot->block.phys + slot->index_offset);
    hel_ring_put(slot->index_count | (XE_ENDIAN_8IN16 << 30));

    if (g_draw_count == 0) {
        hel_log_u32("HEL draw: first draw, physical address", slot->block.phys);
        hel_log_u32("HEL draw: indices", slot->index_count);
        hel_log_u32("HEL draw: vertices", slot->vertex_count);
    }
    g_draw_count++;
    return hel_ring_overflowed() ? HEL_ERR_RING_FULL : HEL_OK;
}

uint32_t hel_mesh_count(void) {
    uint32_t live = 0;
    for (uint32_t i = 0; i < g_mesh_count; ++i) {
        if (g_meshes[i].block.virt && !g_meshes[i].retired) ++live;
    }
    return live;
}

void hel_mesh_release(HelMesh mesh) {
    if (mesh == 0 || mesh > g_mesh_count) return;
    HelMeshSlot *slot = &g_meshes[mesh - 1u];
    if (!slot->block.virt || slot->retired) return;
    slot->retired = 1;
    hel_log_u32("HEL mesh: retired handle", mesh);
}

void hel_mesh_collect_retired(void) {
    for (uint32_t i = 0; i < g_mesh_count; ++i) {
        if (!g_meshes[i].block.virt || !g_meshes[i].retired) continue;
        hel_phys_free(&g_meshes[i].block);
        g_meshes[i].retired = 0;
        g_meshes[i].vertex_count = g_meshes[i].index_count = g_meshes[i].index_offset = 0;
        hel_log_u32("HEL mesh: fenced release handle", i + 1u);
    }
}

void hel_mesh_release_all(void) {
    for (uint32_t i = 0; i < g_mesh_count; ++i) {
        if (g_meshes[i].block.virt) hel_phys_free(&g_meshes[i].block);
        g_meshes[i].retired = 0;
        g_meshes[i].vertex_count = g_meshes[i].index_count = g_meshes[i].index_offset = 0;
    }
    g_mesh_count = 0;
}
