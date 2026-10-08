/*
 * Rendering contract between the demo and the platform backend.
 *
 * Every function returning int returns 0 on success. A non-zero value is one
 * of the HEL_ERR_* codes below and identifies the stage that failed; callers
 * stop submitting frames after any error.
 */
#ifndef HEL_ENGINE_FRAME_H
#define HEL_ENGINE_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float m[16]; } HelMat4;           /* column-major */
/* Position, normal and colour. The normal is carried to the GPU rather than
   consumed at upload, because the scene can switch a light off while running
   and shading baked into the colour could not follow it. */
typedef struct { float x, y, z, nx, ny, nz, u, v, r, g, b, a; } HelVertex;

/* What a draw shades with. The colour is the cooked material's; the light is
   whatever the scene has switched on, already resolved to world space. A
   direction of zero means no directional light, leaving only the ambient
   term, which is how a scene with its light toggled off is drawn. */
typedef struct {
    float base_color[4];     /* material base colour, RGBA 0..1 */
    float light_direction[3];/* unit vector pointing from the surface to the light */
    float ambient[3];        /* ambient colour applied everywhere */
    float light_color[3];    /* directional light colour, already scaled by intensity */
} HelDrawParams;
typedef uint32_t HelMesh;
typedef struct { uint32_t width, height; } HelVideoConfig;

enum {
    HEL_OK = 0,
    HEL_ERR_PLATFORM_ALLOC = 1,   /* physical memory allocation failed */
    HEL_ERR_VIDEO_CONFIG = 2,     /* unsupported width/height */
    HEL_ERR_VIDEO_NOT_READY = 3,  /* video used before hel_video_init */
    HEL_ERR_VIDEO_LAYOUT = 4,     /* EDRAM/front-buffer budget violated */
    HEL_ERR_RING_FULL = 5,        /* frame needs more ring space than exists */
    HEL_ERR_FENCE_TIMEOUT = 6,    /* GPU did not reach the frame fence */
    HEL_ERR_MESH_INVALID = 7,     /* mesh data rejected before upload */
    HEL_ERR_MESH_LIMIT = 8,       /* too many meshes or mesh too large */
    HEL_ERR_SHADER_INVALID = 9    /* shader binary failed validation */
};

/* Platform */
int hel_platform_init(void);
uint64_t hel_ticks(void);
uint64_t hel_tick_frequency(void);
void hel_log(const char *message);
void hel_log_u32(const char *label, uint32_t value); /* prints label=hex */
/* Owned physical allocations (count and bytes) still held by the backend. */
uint32_t hel_platform_allocation_count(void);
uint32_t hel_platform_allocated_bytes(void);
/* Guest heap (heap.c): where the transpiled core's allocations land. */
uint32_t hel_heap_live_allocations(void);
uint32_t hel_heap_used_bytes(void);

/* Video */
int hel_video_init(const HelVideoConfig *config);
int hel_frame_begin(float r, float g, float b, float a);
int hel_mesh_create(const HelVertex *vertices, uint32_t vertex_count,
                    const uint16_t *indices, uint32_t index_count,
                    HelMesh *out_mesh);
/// Retires a model's GPU storage while preserving buffers used by the submitted frame.
void hel_mesh_release(HelMesh mesh);
/// Releases retired model buffers only after the present fence has completed.
void hel_mesh_collect_retired(void);
/// Number of live model uploads, excluding owners already retired for collection.
uint32_t hel_mesh_count(void);
int hel_draw_unlit(HelMesh mesh, const HelMat4 *mvp);
/* As hel_draw_unlit, but shading with the material and light in params.
   The normal is transformed by the model rotation the caller supplies, so a
   rotating object is lit from a fixed direction rather than turning its
   lighting with it. */
int hel_draw_shaded(HelMesh mesh, const HelMat4 *mvp, const HelMat4 *model,
                    const HelDrawParams *params);
/* Draws one batch of interface geometry from the per-frame arena, with the
   positions already in the space `transform` maps to clip space. Depth is
   neither tested nor written, so batches compose in submission order over the
   scene. The data is copied, so the caller's buffers can be reused at once. */
int hel_draw_ui(const HelVertex *vertices, uint32_t vertex_count,
                const uint16_t *indices, uint32_t index_count,
                const HelMat4 *transform, const HelDrawParams *params);
/* Reserves the interface arena; safe to call more than once. */
int hel_ui_arena_init(void);
/// Applies the current authored UI clip to legacy tessellated shapes; shared batches carry their own clips.
int hel_ui_set_scissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/* Textures. A handle of zero means "no texture", which is what an untextured
   draw passes. Pixels are 8 bits a channel in red, green, blue, alpha order,
   row by row with no padding; the backend pads rows to the alignment the
   hardware needs. */
typedef uint32_t HelTexture;
int hel_texture_create(const uint8_t *rgba, uint32_t width, uint32_t height,
                       HelTexture *out_texture);
/// Retires an owned handle without freeing memory still referenced by the current GPU frame.
void hel_texture_release(HelTexture texture);
/// Frees retired uploads after the caller has verified the present fence completed.
void hel_texture_collect_retired(void);
/* Releases every texture. Called at shutdown, like meshes. */
void hel_texture_release_all(void);
uint32_t hel_texture_count(void);

/* Draws one batch of interface geometry sampled from a texture. A handle of
   zero draws the vertex colours alone, which is what hel_draw_ui does. */
int hel_draw_ui_textured(const HelVertex *vertices, uint32_t vertex_count,
                         const uint16_t *indices, uint32_t index_count,
                         const HelMat4 *transform, const HelDrawParams *params,
                         HelTexture texture);
/* Frees the whole arena for reuse. Called when a frame begins. */
void hel_ui_frame_reset(void);
/* The most arena bytes any one frame has used. */
uint32_t hel_ui_peak_bytes(void);
int hel_frame_present(void);
void hel_video_shutdown(void);
uint32_t hel_frame_count(void);

/* Frame lifecycle (engine/frame.c) */
typedef int (*HelFrameFn)(uint32_t frame_index, void *user);
/* Runs frames until fn returns non-zero or frame_limit frames ran (0 = no
   limit). Logs CPU timing every 60 frames. Returns the first error. */
int hel_frame_run(HelFrameFn fn, void *user, uint32_t frame_limit);

#ifdef __cplusplus
}
#endif

#endif
