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

typedef struct { float m[16]; } HelMat4;           /* column-major */
typedef struct { float x, y, z, r, g, b, a; } HelVertex;
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

/* Video */
int hel_video_init(const HelVideoConfig *config);
int hel_frame_begin(float r, float g, float b, float a);
int hel_mesh_create(const HelVertex *vertices, uint32_t vertex_count,
                    const uint16_t *indices, uint32_t index_count,
                    HelMesh *out_mesh);
int hel_draw_unlit(HelMesh mesh, const HelMat4 *mvp);
int hel_frame_present(void);
void hel_video_shutdown(void);
uint32_t hel_frame_count(void);

/* Frame lifecycle (engine/frame.c) */
typedef int (*HelFrameFn)(uint32_t frame_index, void *user);
/* Runs frames until fn returns non-zero or frame_limit frames ran (0 = no
   limit). Logs CPU timing every 60 frames. Returns the first error. */
int hel_frame_run(HelFrameFn fn, void *user, uint32_t frame_limit);

#endif
