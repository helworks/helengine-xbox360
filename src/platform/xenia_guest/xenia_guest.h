/* Internal helpers shared by the Xenia guest backend files. */
#ifndef HEL_XENIA_GUEST_H
#define HEL_XENIA_GUEST_H

#include <stdint.h>

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
int hel_shaders_validate(void);
void hel_shaders_emit(void);

/* Demo entry called from start.S. */
void hel_demo_main(void);

#endif
