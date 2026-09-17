/* Internal helpers shared by the Xenia guest backend files. */
#ifndef HEL_XENIA_GUEST_H
#define HEL_XENIA_GUEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A physical-memory block: guest virtual address (0xA0000000 alias, 64 KiB
   pages), physical address for GPU fields, and size in bytes. */
typedef struct {
    uint32_t virt;
    uint32_t phys;
    uint32_t size;
} HelPhysBlock;

int hel_phys_alloc(const char *name, uint32_t size, HelPhysBlock *out);
void hel_phys_free(HelPhysBlock *block);
void hel_sleep_ms(uint32_t milliseconds);
uint32_t hel_platform_allocation_count(void);
uint32_t hel_platform_allocated_bytes(void);

/* Ring-buffer writer (video.c). Valid between hel_frame_begin and
   hel_frame_present; writes past the per-frame budget set the overflow flag
   that hel_frame_present reports as HEL_ERR_RING_FULL. */
void hel_ring_put(uint32_t value);
void hel_ring_reg(uint32_t index, uint32_t value);
void hel_ring_type3(uint32_t opcode, uint32_t count);
int hel_ring_overflowed(void);
uint32_t hel_video_width(void);
uint32_t hel_video_height(void);

/* Mesh storage (mesh.c): released by hel_video_shutdown. */
void hel_mesh_release_all(void);

/* Shader binaries (shader.c): validated once, then emitted per draw. */
/* Which vertex shader a draw uses; both share the pixel shader. */
typedef enum { HEL_SHADER_UNLIT = 0, HEL_SHADER_SHADED = 1 } HelShaderKind;

int hel_shaders_validate(void);
void hel_shaders_emit(void);
void hel_shaders_emit_kind(HelShaderKind kind);

/* General-purpose heap (heap.c): the C++ runtime and the transpiled engine
   core allocate from here. GPU-visible memory uses hel_phys_alloc instead. */
int hel_heap_init(uint32_t bytes);
void *hel_malloc(uint32_t size);
void *hel_realloc(void *pointer, uint32_t size);
void hel_free(void *pointer);
uint32_t hel_heap_used_bytes(void);
uint32_t hel_heap_peak_bytes(void);
uint32_t hel_heap_live_allocations(void);
uint32_t hel_heap_total_allocations(void);
uint32_t hel_heap_failed_allocations(void);
void hel_heap_report(const char *label);
void hel_heap_shutdown(void);

/* Cooked content archive linked into the image (content.c). The archive is
   built by tools/pack-content.py; an image with no staged content links an
   empty one, so every lookup simply reports "not present". */
int hel_content_init(void);
int hel_content_open(const char *path, const uint8_t **data, uint32_t *size);
uint32_t hel_content_count(void);
uint32_t hel_content_lookups(void);
uint32_t hel_content_misses(void);
const char *hel_content_path_at(uint32_t index);

/* C++ runtime support (cxx_runtime.cpp). hel_cxx_init runs the static
   constructors in .init_array and must run after hel_heap_init. */
void hel_cxx_init(void);
uint32_t hel_cxx_static_initialiser_count(void);

/* Stops the guest after a fatal condition: logs nothing itself, spins so the
   emulator window and log stay inspectable. Never returns (start.S). */
void hel_halt(void);

/* Demo entry called from start.S. */
void hel_demo_main(void);

#ifdef __cplusplus
}
#endif

#endif
