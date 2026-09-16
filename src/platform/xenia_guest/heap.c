/*
 * General-purpose guest heap.
 *
 * One physical block (hel_phys_alloc) is carved into 16-byte-aligned chunks
 * by a first-fit free list with coalescing on free. The C++ runtime's
 * operator new and the transpiled engine core allocate from here; memory the
 * GPU reads still comes from hel_phys_alloc directly, because those blocks
 * need their own physical addresses and lifetimes.
 *
 * The block header is 16 bytes, so the payload of every allocation is
 * 16-byte aligned, which satisfies the alignment of every type the PowerPC
 * ABI defines (including doubles and the 8-byte-aligned types the generated
 * core uses).
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"

#define HEL_HEAP_ALIGN 16u

typedef struct HelHeapNode {
    uint32_t size; /* payload bytes, always a multiple of HEL_HEAP_ALIGN */
    uint32_t used;
    struct HelHeapNode *next;
    struct HelHeapNode *prev;
} HelHeapNode;

static HelPhysBlock g_block;
static HelHeapNode *g_first;
static uint32_t g_used_bytes;
static uint32_t g_peak_bytes;
static uint32_t g_live_allocations;
static uint32_t g_total_allocations;
static uint32_t g_failed_allocations;

int hel_heap_init(uint32_t bytes) {
    if (g_first) {
        return HEL_OK;
    }
    bytes = (bytes + 0xFFFFu) & ~0xFFFFu;
    int err = hel_phys_alloc("heap", bytes, &g_block);
    if (err) {
        return err;
    }
    g_first = (HelHeapNode *)(uintptr_t)g_block.virt;
    g_first->size = bytes - (uint32_t)sizeof(HelHeapNode);
    g_first->used = 0;
    g_first->next = 0;
    g_first->prev = 0;
    hel_log_u32("HEL heap: bytes", bytes);
    hel_log_u32("HEL heap: usable bytes", g_first->size);
    return HEL_OK;
}

static int heap_owns(const void *pointer) {
    uint32_t address = (uint32_t)(uintptr_t)pointer;
    return g_block.virt != 0 && address > g_block.virt &&
           address < g_block.virt + g_block.size;
}

void *hel_malloc(uint32_t size) {
    if (!g_first) {
        hel_log("HEL heap: allocation before hel_heap_init");
        g_failed_allocations++;
        return 0;
    }
    uint32_t need = size ? size : 1u; /* a zero-byte request needs a unique
                                         address, so give it one chunk */
    need = (need + HEL_HEAP_ALIGN - 1u) & ~(HEL_HEAP_ALIGN - 1u);
    for (HelHeapNode *node = g_first; node; node = node->next) {
        if (node->used || node->size < need) {
            continue;
        }
        /* Split only when the remainder can hold a header and one chunk. */
        if (node->size >= need + sizeof(HelHeapNode) + HEL_HEAP_ALIGN) {
            HelHeapNode *rest =
                (HelHeapNode *)((uint8_t *)(node + 1) + need);
            rest->size = node->size - need - (uint32_t)sizeof(HelHeapNode);
            rest->used = 0;
            rest->next = node->next;
            rest->prev = node;
            if (node->next) {
                node->next->prev = rest;
            }
            node->next = rest;
            node->size = need;
        }
        node->used = 1;
        g_used_bytes += node->size;
        if (g_used_bytes > g_peak_bytes) {
            g_peak_bytes = g_used_bytes;
        }
        g_live_allocations++;
        g_total_allocations++;
        return node + 1;
    }
    hel_log_u32("HEL heap: out of memory, requested", size);
    hel_log_u32("HEL heap: in use", g_used_bytes);
    g_failed_allocations++;
    return 0;
}

static void merge_with_next(HelHeapNode *node) {
    HelHeapNode *victim = node->next;
    node->size += (uint32_t)sizeof(HelHeapNode) + victim->size;
    node->next = victim->next;
    if (victim->next) {
        victim->next->prev = node;
    }
}

void hel_free(void *pointer) {
    if (!pointer) {
        return;
    }
    if (!heap_owns(pointer)) {
        hel_log_u32("HEL heap: free of a foreign pointer",
                    (uint32_t)(uintptr_t)pointer);
        return;
    }
    HelHeapNode *node = (HelHeapNode *)pointer - 1;
    if (!node->used) {
        hel_log_u32("HEL heap: double free ignored",
                    (uint32_t)(uintptr_t)pointer);
        return;
    }
    node->used = 0;
    g_used_bytes -= node->size;
    g_live_allocations--;
    if (node->next && !node->next->used) {
        merge_with_next(node);
    }
    if (node->prev && !node->prev->used) {
        merge_with_next(node->prev);
    }
}

void *hel_realloc(void *pointer, uint32_t size) {
    if (!pointer) {
        return hel_malloc(size);
    }
    if (!heap_owns(pointer)) {
        hel_log_u32("HEL heap: realloc of a foreign pointer",
                    (uint32_t)(uintptr_t)pointer);
        return 0;
    }
    HelHeapNode *node = (HelHeapNode *)pointer - 1;
    if (node->size >= size) {
        return pointer;
    }
    void *fresh = hel_malloc(size);
    if (!fresh) {
        return 0;
    }
    const uint8_t *src = (const uint8_t *)pointer;
    uint8_t *dst = (uint8_t *)fresh;
    for (uint32_t i = 0; i < node->size; ++i) {
        dst[i] = src[i];
    }
    hel_free(pointer);
    return fresh;
}

uint32_t hel_heap_used_bytes(void) { return g_used_bytes; }
uint32_t hel_heap_peak_bytes(void) { return g_peak_bytes; }
uint32_t hel_heap_live_allocations(void) { return g_live_allocations; }
uint32_t hel_heap_total_allocations(void) { return g_total_allocations; }
uint32_t hel_heap_failed_allocations(void) { return g_failed_allocations; }

void hel_heap_report(const char *label) {
    hel_log(label);
    hel_log_u32("HEL heap: in use", g_used_bytes);
    hel_log_u32("HEL heap: peak", g_peak_bytes);
    hel_log_u32("HEL heap: live allocations", g_live_allocations);
    hel_log_u32("HEL heap: total allocations", g_total_allocations);
    hel_log_u32("HEL heap: failed allocations", g_failed_allocations);
}

void hel_heap_shutdown(void) {
    if (!g_first) {
        return;
    }
    if (g_live_allocations != 0) {
        hel_log_u32("HEL heap: leaked allocations at shutdown",
                    g_live_allocations);
        hel_log_u32("HEL heap: leaked bytes", g_used_bytes);
    }
    hel_phys_free(&g_block);
    g_first = 0;
    g_used_bytes = 0;
    g_live_allocations = 0;
}
