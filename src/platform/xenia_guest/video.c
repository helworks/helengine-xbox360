/*
 * Video backend for the Xenia guest: ring-buffer submission, EDRAM render
 * target layout, clear and present through resolves and VdSwap, and the
 * per-frame GPU fence. Register, packet and kernel-call facts are recorded in
 * docs/xenia-backend-contract.md; this file emits exactly those.
 *
 * Ownership: one frame is in flight at a time. hel_frame_present waits for
 * the frame's fence before returning, so the ring region written by the next
 * frame is never still being read by the GPU.
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xboxkrnl.h"
#include "xenia_guest.h"
#include "xenos_regs.h"

/* Shared authored text batches upload validated shader pairs per run. A
   1 MiB ring reserves 512 KiB for a contiguous frame; the prior 32 KiB
   frame budget exhausted during DemoDisc's first outlined-text pass. */
#define HEL_RING_SIZE_LOG2 17u /* VdInitializeRingBuffer: bytes = 1 << (17+3) */
#define HEL_RING_BYTES (1u << (HEL_RING_SIZE_LOG2 + 3u))
#define HEL_RING_DWORDS (HEL_RING_BYTES / 4u)
#define HEL_MAX_FRAME_DWORDS (HEL_RING_DWORDS / 2u)
#define HEL_SWAP_SLOT_DWORDS 64u
#define HEL_SCRATCH_BYTES 0x10000u
#define HEL_FRONT_BYTES (64u * 0x10000u) /* >= 1280x736x4 tiled 8_8_8_8 */
#define HEL_FENCE_TIMEOUT_MS 5000u
#define HEL_MAX_WIDTH 1280u
#define HEL_MAX_HEIGHT 720u

/* Scratch page layout (byte offsets). */
#define SCR_RPTR_WRITEBACK 0x000u
#define SCR_FENCE 0x040u
#define SCR_SWAP_ARGS 0x080u /* frontbuffer, format, color space, width, height */
#define SCR_SYS_WRITEBACK 0x0C0u
#define SCR_SYS_COMMAND 0x0C4u
#define SCR_FETCH 0x100u /* 6 dwords: front-buffer texture fetch constant */
#define SCR_RECT 0x140u  /* 6 floats: resolve rectangle vertices */

static struct {
    int ready;
    uint32_t width, height;
    uint32_t color_tiles;
    HelPhysBlock ring, scratch, front;
    volatile uint32_t *ring_dw;
    uint32_t write_index;
    uint32_t frame_start_index;
    int overflow;
    uint32_t frames_begun;
    uint32_t frames_presented;
    uint32_t fence_value;
    uint32_t clear_color;
    uint64_t last_present_ticks;
} g;

static inline volatile uint32_t *scratch_u32(uint32_t offset) {
    return (volatile uint32_t *)(uintptr_t)(g.scratch.virt + offset);
}

static inline volatile float *scratch_f32(uint32_t offset) {
    return (volatile float *)(uintptr_t)(g.scratch.virt + offset);
}

/* Ring writer ------------------------------------------------------------ */

static void ring_put(uint32_t value) {
    if (g.write_index >= HEL_RING_DWORDS ||
        g.write_index - g.frame_start_index >= HEL_MAX_FRAME_DWORDS) {
        g.overflow = 1;
        return;
    }
    g.ring_dw[g.write_index++] = value;
}

static void ring_reg(uint32_t index, uint32_t value) {
    ring_put(XE_PACKET_TYPE0(index, 1));
    ring_put(value);
}

void hel_ring_put(uint32_t value) { ring_put(value); }

void hel_ring_reg(uint32_t index, uint32_t value) { ring_reg(index, value); }

void hel_ring_type3(uint32_t opcode, uint32_t count) {
    ring_put(XE_PACKET_TYPE3(opcode, count));
}

int hel_ring_overflowed(void) { return g.overflow; }

uint32_t hel_ring_available(void) {
    if (!g.ready || g.overflow || g.write_index < g.frame_start_index ||
        g.write_index >= HEL_RING_DWORDS) return 0;
    uint32_t used = g.write_index - g.frame_start_index;
    if (used >= HEL_MAX_FRAME_DWORDS) return 0;
    uint32_t frame = HEL_MAX_FRAME_DWORDS - used;
    uint32_t contiguous = HEL_RING_DWORDS - g.write_index;
    return frame < contiguous ? frame : contiguous;
}

uint32_t hel_video_width(void) { return g.width; }

uint32_t hel_video_height(void) { return g.height; }

static void ring_frame_start(void) {
    /* Keep every frame contiguous: pad to the end with NOPs when the frame's
       worst case would cross the ring end, then continue from index 0. */
    if (g.write_index + HEL_MAX_FRAME_DWORDS > HEL_RING_DWORDS) {
        while (g.write_index < HEL_RING_DWORDS) {
            g.ring_dw[g.write_index++] = XE_PACKET_TYPE2();
        }
        g.write_index = 0;
    }
    g.frame_start_index = g.write_index;
    g.overflow = 0;
}

static void ring_kick(void) {
    volatile uint32_t *wptr =
        (volatile uint32_t *)(uintptr_t)(XE_GPU_MMIO_BASE +
                                         XE_GPU_REG_CP_RB_WPTR * 4u);
    *wptr = g.write_index;
}

/* GPU state -------------------------------------------------------------- */

static uint32_t pack_8888(float r, float g_, float b, float a) {
    uint32_t channels[4];
    float values[4] = {r, g_, b, a};
    for (int i = 0; i < 4; ++i) {
        float v = values[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        channels[i] = (uint32_t)(v * 255.0f + 0.5f);
    }
    return channels[0] | (channels[1] << 8) | (channels[2] << 16) |
           (channels[3] << 24);
}

static void emit_frame_state(void) {
    ring_reg(XE_GPU_REG_RB_SURFACE_INFO, g.width);           /* pitch, 1x */
    ring_reg(XE_GPU_REG_RB_COLOR_INFO, 0u);                  /* tile 0, 8888 */
    ring_reg(XE_GPU_REG_RB_DEPTH_INFO, g.color_tiles);       /* after color */
    ring_reg(XE_GPU_REG_RB_COLOR_MASK, 0xFu);
    ring_reg(XE_GPU_REG_PA_SC_WINDOW_OFFSET, 0u);
    ring_reg(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 1u << 31); /* offset off */
    ring_reg(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, g.width | (g.height << 16));
    ring_reg(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, 0u);
    ring_reg(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, g.width | (g.height << 16));
    ring_reg(XE_GPU_REG_PA_SU_SC_MODE_CNTL, 0u);
    ring_reg(XE_GPU_REG_PA_SU_VTX_CNTL, 0u); /* Direct3D pixel centres */
    ring_reg(XE_GPU_REG_PA_SC_VIZ_QUERY, 0u);
    ring_reg(XE_GPU_REG_VGT_OUTPUT_PATH_CNTL, 0u);
    ring_reg(XE_GPU_REG_VGT_MAX_VTX_INDX, 0xFFFFFFu);
    ring_reg(XE_GPU_REG_VGT_MIN_VTX_INDX, 0u);
    ring_reg(XE_GPU_REG_VGT_INDX_OFFSET, 0u);
    ring_reg(XE_GPU_REG_RB_MODECONTROL, XE_EDRAM_MODE_COLOR_DEPTH);
}

/* Resolve colour target 0 into the front buffer; clear_flags may add
   XE_COPY_COLOR_CLEAR_ENABLE / XE_COPY_DEPTH_CLEAR_ENABLE, which clear the
   EDRAM surfaces after the copy. */
static void emit_resolve(uint32_t clear_flags) {
    uint32_t rect_phys = g.scratch.phys + SCR_RECT;
    ring_reg(XE_GPU_REG_RB_MODECONTROL, XE_EDRAM_MODE_COPY);
    ring_reg(XE_GPU_REG_RB_COPY_CONTROL, clear_flags); /* src colour 0, raw */
    ring_reg(XE_GPU_REG_RB_COPY_DEST_BASE, g.front.phys);
    ring_reg(XE_GPU_REG_RB_COPY_DEST_PITCH, g.width | (g.height << 16));
    ring_reg(XE_GPU_REG_RB_COPY_DEST_INFO,
             XE_ENDIAN_8IN32 | (XE_TEXTURE_FORMAT_8_8_8_8 << 7));
    ring_reg(XE_GPU_REG_RB_DEPTH_CLEAR, 0xFFFFFF00u); /* depth 1.0, stencil 0 */
    ring_reg(XE_GPU_REG_RB_COLOR_CLEAR, g.clear_color);
    ring_reg(XE_GPU_REG_RB_COLOR_CLEAR_LO, 0u);
    /* Vertex fetch constant 0: three float2 corners, big-endian floats. */
    ring_put(XE_PACKET_TYPE0(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 2));
    ring_put(3u | (rect_phys & ~3u));
    ring_put(XE_ENDIAN_8IN32 | (6u << 2));
    ring_put(XE_PACKET_TYPE3(PM4_DRAW_INDX, 2));
    ring_put(0u); /* no visibility query */
    ring_put(XE_DRAW_INITIATOR(XE_PRIM_RECTANGLE_LIST,
                               XE_SOURCE_SELECT_AUTO_INDEX, 3u));
    ring_reg(XE_GPU_REG_RB_MODECONTROL, XE_EDRAM_MODE_COLOR_DEPTH);
}

static void write_front_buffer_fetch_constant(void) {
    volatile uint32_t *fetch = scratch_u32(SCR_FETCH);
    fetch[0] = 2u | (2u << 10) | (2u << 13) | (2u << 16) |
               ((g.width >> 5) << 22) | (1u << 31);
    fetch[1] = XE_TEXTURE_FORMAT_8_8_8_8 | (XE_ENDIAN_8IN32 << 6) |
               (g.front.virt & 0xFFFFF000u);
    fetch[2] = (g.width - 1u) | ((g.height - 1u) << 13);
    fetch[3] = 0x688u << 1; /* swizzle XYZW, point filters */
    fetch[4] = 0u;
    fetch[5] = 1u << 9; /* 2D */
    volatile uint32_t *args = scratch_u32(SCR_SWAP_ARGS);
    args[0] = g.front.virt;
    args[1] = XE_TEXTURE_FORMAT_8_8_8_8;
    args[2] = 0u; /* RGB colour space */
    args[3] = g.width;
    args[4] = g.height;
    volatile float *rect = scratch_f32(SCR_RECT);
    float x0 = -0.5f, y0 = -0.5f;
    float x1 = (float)g.width - 0.5f, y1 = (float)g.height - 0.5f;
    rect[0] = x0; rect[1] = y0;
    rect[2] = x1; rect[3] = y0;
    rect[4] = x0; rect[5] = y1;
}

/* Public interface ------------------------------------------------------- */

int hel_video_init(const HelVideoConfig *config) {
    int err;
    if (g.ready) {
        return HEL_OK;
    }
    if (config == 0 || config->width == 0 || config->height == 0 ||
        config->width > HEL_MAX_WIDTH || config->height > HEL_MAX_HEIGHT ||
        (config->width & 7u) != 0 || (config->height & 7u) != 0) {
        hel_log("HEL video: rejected configuration (width/height must be "
                "multiples of 8 and at most 1280x720)");
        if (config) {
            hel_log_u32("HEL video: requested width", config->width);
            hel_log_u32("HEL video: requested height", config->height);
        }
        return HEL_ERR_VIDEO_CONFIG;
    }
    g.width = config->width;
    g.height = config->height;
    uint32_t tiles_x = (g.width + XE_EDRAM_TILE_WIDTH - 1u) / XE_EDRAM_TILE_WIDTH;
    uint32_t tiles_y = (g.height + XE_EDRAM_TILE_HEIGHT - 1u) / XE_EDRAM_TILE_HEIGHT;
    g.color_tiles = tiles_x * tiles_y;
    uint32_t front_bytes = ((g.width + 31u) & ~31u) * ((g.height + 31u) & ~31u) * 4u;
    hel_log_u32("HEL video: mode width", g.width);
    hel_log_u32("HEL video: mode height", g.height);
    hel_log_u32("HEL video: edram tiles per surface", g.color_tiles);
    hel_log_u32("HEL video: edram tiles total", g.color_tiles * 2u);
    hel_log_u32("HEL video: front buffer bytes needed", front_bytes);
    if (g.color_tiles * 2u > XE_EDRAM_TILE_COUNT || front_bytes > HEL_FRONT_BYTES) {
        hel_log("HEL video: layout exceeds EDRAM or front-buffer budget");
        return HEL_ERR_VIDEO_LAYOUT;
    }

    err = hel_phys_alloc("ring", HEL_RING_BYTES, &g.ring);
    if (err) goto fail;
    err = hel_phys_alloc("scratch", HEL_SCRATCH_BYTES, &g.scratch);
    if (err) goto fail;
    err = hel_phys_alloc("frontbuffer", HEL_FRONT_BYTES, &g.front);
    if (err) goto fail;
    /* The interface arena, allocated here so a scene that draws no
       interface still pays for it once rather than on first use, where a
       failure would arrive mid-frame. */
    err = hel_ui_arena_init();
    if (err) goto fail;
    if ((g.front.phys & 0xFFFu) != 0 || (g.ring.phys & 0xFFFu) != 0) {
        hel_log("HEL video: allocation alignment unexpected");
        err = HEL_ERR_VIDEO_LAYOUT;
        goto fail;
    }

    g.ring_dw = (volatile uint32_t *)(uintptr_t)g.ring.virt;
    g.write_index = 0;
    g.frame_start_index = 0;
    g.fence_value = 0;
    write_front_buffer_fetch_constant();

    VdInitializeRingBuffer(g.ring.phys, HEL_RING_SIZE_LOG2);
    VdEnableRingBufferRPtrWriteBack(g.scratch.phys + SCR_RPTR_WRITEBACK, 6);
    hel_log_u32("HEL video: ring initialised, bytes", HEL_RING_BYTES);
    hel_log_u32("HEL video: allocations", hel_platform_allocation_count());
    hel_log_u32("HEL video: allocated bytes", hel_platform_allocated_bytes());
    g.ready = 1;
    return HEL_OK;

fail:
    hel_log_u32("HEL video: init failed, stage code", (uint32_t)err);
    hel_phys_free(&g.front);
    hel_phys_free(&g.scratch);
    hel_phys_free(&g.ring);
    return err;
}

int hel_frame_begin(float r, float g_, float b, float a) {
    if (!g.ready) {
        return HEL_ERR_VIDEO_NOT_READY;
    }
    ring_frame_start();
    /* The previous frame's interface geometry has been consumed by now:
       this function is only reached after hel_frame_present blocked on
       that frame's fence, so the whole arena is free again. */
    hel_ui_frame_reset();
    hel_shaders_frame_reset();
    g.clear_color = pack_8888(r, g_, b, a);
    emit_frame_state();
    emit_resolve(XE_COPY_COLOR_CLEAR_ENABLE | XE_COPY_DEPTH_CLEAR_ENABLE);
    g.frames_begun++;
    return g.overflow ? HEL_ERR_RING_FULL : HEL_OK;
}

int hel_frame_present(void) {
    if (!g.ready) {
        return HEL_ERR_VIDEO_NOT_READY;
    }
    emit_resolve(0u);

    /* VdSwap fills a 64-dword slot that the guest reserves in the ring. */
    if (g.write_index + HEL_SWAP_SLOT_DWORDS > HEL_RING_DWORDS ||
        g.write_index + HEL_SWAP_SLOT_DWORDS - g.frame_start_index >
            HEL_MAX_FRAME_DWORDS) {
        g.overflow = 1;
    }
    if (g.overflow) {
        return HEL_ERR_RING_FULL;
    }
    uint32_t slot_virt = g.ring.virt + g.write_index * 4u;
    g.write_index += HEL_SWAP_SLOT_DWORDS;
    volatile uint32_t *args = scratch_u32(SCR_SWAP_ARGS);
    hel_vdswap(slot_virt, (const uint32_t *)scratch_u32(SCR_FETCH),
               (uint32_t *)scratch_u32(SCR_SYS_WRITEBACK),
               (uint32_t *)scratch_u32(SCR_SYS_COMMAND), 0xBEEF0001u,
               (const uint32_t *)&args[0], (const uint32_t *)&args[1],
               (const uint32_t *)&args[2], (const uint32_t *)&args[3],
               (const uint32_t *)&args[4]);

    /* Fence: the command processor writes the value when it reaches this. */
    g.fence_value++;
    ring_put(XE_PACKET_TYPE3(PM4_EVENT_WRITE_SHD, 3));
    ring_put(XE_EVENT_PS_DONE_TS);
    ring_put((g.scratch.phys + SCR_FENCE) | XE_ENDIAN_8IN32);
    ring_put(g.fence_value);
    if (g.overflow) {
        return HEL_ERR_RING_FULL;
    }

    uint64_t submit_ticks = hel_ticks();
    ring_kick();

    volatile uint32_t *fence = scratch_u32(SCR_FENCE);
    uint32_t waited_ms = 0;
    while (*fence != g.fence_value) {
        if (waited_ms >= HEL_FENCE_TIMEOUT_MS) {
            hel_log_u32("HEL video: fence timeout, expected", g.fence_value);
            hel_log_u32("HEL video: fence value seen", *fence);
            return HEL_ERR_FENCE_TIMEOUT;
        }
        hel_sleep_ms(1);
        waited_ms++;
    }
    uint64_t done_ticks = hel_ticks();
    /* Scene disposal can happen before present. Retired textures remain valid
       for every recorded draw until this fence acknowledges GPU completion. */
    hel_texture_collect_retired();
    hel_mesh_collect_retired();
    g.frames_presented++;
    g.last_present_ticks = done_ticks;
    if (g.frames_presented == 1) {
        hel_log("HEL video: first present complete");
        hel_log_u32("HEL video: first frame ring dwords",
                    g.write_index - g.frame_start_index);
        hel_log_u32("HEL video: first frame gpu wait ms", waited_ms);
        hel_log_u32("HEL video: first frame submit->done ticks",
                    (uint32_t)(done_ticks - submit_ticks));
    }
    return HEL_OK;
}

uint32_t hel_frame_count(void) { return g.frames_presented; }

void hel_video_shutdown(void) {
    if (!g.ready) {
        return;
    }
    /* All submitted work completed at the last fence wait; nothing pending. */
    g.ready = 0;
    hel_mesh_release_all();
    hel_phys_free(&g.front);
    hel_phys_free(&g.scratch);
    hel_phys_free(&g.ring);
    hel_log_u32("HEL video: shutdown, allocations left",
                hel_platform_allocation_count());
}
