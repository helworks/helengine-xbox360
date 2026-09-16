/*
 * Platform services for the Xenia guest: logging through DbgPrint, monotonic
 * time from the PowerPC time base, sleeping and physical memory allocation
 * through xboxkrnl.exe exports (docs/xenia-backend-contract.md sections 1-2).
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xboxkrnl.h"
#include "xenia_guest.h"

static uint64_t g_tick_frequency;
static uint32_t g_allocation_count;
static uint32_t g_allocated_bytes;

void hel_log(const char *message) { DbgPrint("%s", message); }

void hel_log_u32(const char *label, uint32_t value) {
    DbgPrint("%s=%08X", label, value);
}

int hel_platform_init(void) {
    /* The time base frequency is the emulator's host tick frequency, not the
       console's 50 MHz, so it must be queried (contract section 1). */
    g_tick_frequency = KeQueryPerformanceFrequency();
    if (g_tick_frequency == 0) {
        hel_log("HEL platform: KeQueryPerformanceFrequency returned 0");
        return HEL_ERR_PLATFORM_ALLOC;
    }
    hel_log_u32("HEL platform: tick_frequency", (uint32_t)g_tick_frequency);
    hel_log_u32("HEL platform: timebase_now_lo", (uint32_t)hel_read_timebase());
    return HEL_OK;
}

uint64_t hel_ticks(void) { return hel_read_timebase(); }

uint64_t hel_tick_frequency(void) { return g_tick_frequency; }

void hel_sleep_ms(uint32_t milliseconds) {
    int64_t interval = -(int64_t)milliseconds * 10000; /* 100 ns units */
    KeDelayExecutionThread(1, 0, &interval);
}

int hel_phys_alloc(const char *name, uint32_t size, HelPhysBlock *out) {
    uint32_t virt = MmAllocatePhysicalMemoryEx(
        0, size, X_PAGE_READWRITE | X_MEM_LARGE_PAGES, 0, 0xFFFFFFFFu, 0);
    if (virt == 0) {
        hel_log(name);
        hel_log_u32("HEL platform: physical allocation failed, size", size);
        out->virt = out->phys = out->size = 0;
        return HEL_ERR_PLATFORM_ALLOC;
    }
    volatile uint32_t *words = (volatile uint32_t *)(uintptr_t)virt;
    for (uint32_t i = 0; i < size / 4u; ++i) {
        words[i] = 0;
    }
    out->virt = virt;
    out->phys = MmGetPhysicalAddress(virt);
    out->size = size;
    g_allocation_count++;
    g_allocated_bytes += size;
    DbgPrint("HEL platform: alloc %s virt=%08X phys=%08X size=%08X", name,
             virt, out->phys, size);
    return HEL_OK;
}

void hel_phys_free(HelPhysBlock *block) {
    if (block->virt != 0) {
        MmFreePhysicalMemory(0, block->virt);
        g_allocation_count--;
        g_allocated_bytes -= block->size;
        block->virt = block->phys = block->size = 0;
    }
}

uint32_t hel_platform_allocation_count(void) { return g_allocation_count; }

uint32_t hel_platform_allocated_bytes(void) { return g_allocated_bytes; }
