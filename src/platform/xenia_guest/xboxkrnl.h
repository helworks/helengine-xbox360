/*
 * Declarations for the xboxkrnl.exe exports the Xenia guest backend uses.
 *
 * Every function here is reached through an XEX2 import thunk (imports.inc
 * lists the ordinals). Argument conventions and flag values follow the pinned
 * xenia-canary implementation documented in docs/xenia-backend-contract.md.
 * Nothing in this header comes from Microsoft SDK material.
 */
#ifndef HEL_XBOXKRNL_H
#define HEL_XBOXKRNL_H

#include <stdint.h>

/* Protection and allocation flags for MmAllocatePhysicalMemoryEx. */
#define X_PAGE_READONLY 0x00000002u
#define X_PAGE_READWRITE 0x00000004u
#define X_PAGE_NOCACHE 0x00000200u
#define X_PAGE_WRITECOMBINE 0x00000400u
#define X_MEM_LARGE_PAGES 0x20000000u /* 64 KiB pages, 0xA0000000 alias */

uint32_t DbgPrint(const char *format, ...);
uint32_t KeQueryPerformanceFrequency(void);
/* interval: relative time in 100 ns units, negative (e.g. -10000 = 1 ms). */
uint32_t KeDelayExecutionThread(uint32_t processor_mode, uint32_t alertable,
                                const int64_t *interval);
uint32_t MmAllocatePhysicalMemoryEx(uint32_t flags, uint32_t size,
                                    uint32_t protect, uint32_t min_physical,
                                    uint32_t max_physical, uint32_t alignment);
void MmFreePhysicalMemory(uint32_t type, uint32_t base_address);
uint32_t MmGetPhysicalAddress(uint32_t virtual_address);
void VdInitializeRingBuffer(uint32_t physical_address, uint32_t size_log2);
void VdEnableRingBufferRPtrWriteBack(uint32_t physical_address,
                                     uint32_t block_size_log2);

/*
 * VdSwap takes ten arguments. The kernel reads the ninth and tenth from
 * r1+0x54 and r1+0x5C, which is not where the SVR4 ABI places them, so calls
 * go through this assembly wrapper (start.S).
 */
void hel_vdswap(uint32_t ring_slot_virtual, const uint32_t *fetch_constant,
                uint32_t *writeback, uint32_t *system_command_buffer,
                uint32_t system_command_buffer_id,
                const uint32_t *frontbuffer_virtual,
                const uint32_t *texture_format, const uint32_t *color_space,
                const uint32_t *width, const uint32_t *height);

/* start.S helpers. */
uint64_t hel_read_timebase(void);
void hel_syscall_marker(uint32_t value);

#endif
