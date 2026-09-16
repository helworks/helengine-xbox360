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

/* Demo entry called from start.S. */
void hel_demo_main(void);

#endif
