/*
 * Frame lifecycle: runs the demo's frame function, counts frames, measures
 * CPU time per frame (submission side only), reports the backend's owned
 * allocations so a long run exposes growth, and stops on the first error.
 * Logging uses fixed buffers in the platform layer; it allocates nothing.
 */
#include "frame.h"

int hel_frame_run(HelFrameFn fn, void *user, uint32_t frame_limit) {
    uint64_t frequency = hel_tick_frequency();
    uint64_t window_start = hel_ticks();
    uint64_t cpu_ticks = 0;
    for (uint32_t index = 0; frame_limit == 0 || index < frame_limit; ++index) {
        uint64_t t0 = hel_ticks();
        int err = fn(index, user);
        uint64_t t1 = hel_ticks();
        cpu_ticks += t1 - t0;
        if (err != HEL_OK) {
            hel_log_u32("HEL frame: error code", (uint32_t)err);
            hel_log_u32("HEL frame: failed at frame", index);
            return err;
        }
        if ((index + 1u) % 60u == 0u && frequency != 0) {
            uint64_t now = hel_ticks();
            uint32_t cpu_us = (uint32_t)((cpu_ticks * 1000000u) / (frequency * 60u));
            uint32_t window_ms = (uint32_t)(((now - window_start) * 1000u) / frequency);
            hel_log_u32("HEL frame: frames", index + 1u);
            hel_log_u32("HEL frame: avg cpu us per frame", cpu_us);
            hel_log_u32("HEL frame: last 60 frames ms", window_ms);
            hel_log_u32("HEL frame: presented", hel_frame_count());
            hel_log_u32("HEL frame: allocations", hel_platform_allocation_count());
            hel_log_u32("HEL frame: allocated bytes", hel_platform_allocated_bytes());
            /* Heap counters too: the transpiled core allocates from the heap,
               so a managed leak shows up here rather than in the physical
               allocation count. */
            hel_log_u32("HEL frame: heap allocations", hel_heap_live_allocations());
            hel_log_u32("HEL frame: heap bytes", hel_heap_used_bytes());
            window_start = now;
            cpu_ticks = 0;
        }
    }
    return HEL_OK;
}
