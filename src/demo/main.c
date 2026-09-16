/*
 * Demo entry (called from the platform start code).
 *
 * HEL_DEMO_MODE selects the diagnostic: 0 alternates two clear colours every
 * 60 submitted frames, 1 draws the static unlit triangle, 2 the rotating
 * cube. Building with -DHEL_DEMO_INVALID_CONFIG=1 requests an unsupported
 * video mode to exercise the failure path (diagnostic logged, no submission);
 * -DHEL_DEMO_FRAME_LIMIT=<n> stops after n frames and shuts video down so the
 * teardown path can be checked (default 0 = run until the emulator closes).
 */
#include <stdint.h>

#include "../engine/frame.h"
#include "../platform/xenia_guest/xenia_guest.h"
#include "demo.h"

#ifndef HEL_DEMO_MODE
#define HEL_DEMO_MODE HEL_DEMO_MODE_CLEAR
#endif
#ifndef HEL_DEMO_FRAME_LIMIT
#define HEL_DEMO_FRAME_LIMIT 0u
#endif
#ifndef HEL_CORE_BOOT
#define HEL_CORE_BOOT 1
#endif
#ifndef HEL_HEAP_BYTES
#define HEL_HEAP_BYTES (32u * 1024u * 1024u)
#endif

#if HEL_CORE_BOOT
/* Runs one managed frame before the scene's own frame, so the generated
   core advances on the console's clock while the backend keeps drawing. */
static HelFrameFn g_scene_frame;
static uint64_t g_last_ticks;

static int core_and_scene_frame(uint32_t frame, void *user) {
    uint64_t now = hel_ticks();
    uint64_t frequency = hel_tick_frequency();
    double elapsed = 0.0;
    if (g_last_ticks != 0 && frequency != 0) {
        elapsed = (double)(now - g_last_ticks) / (double)frequency;
    }
    g_last_ticks = now;
    int err = hel_core_update(elapsed);
    if (!err) {
        err = g_scene_frame(frame, user);
    }
    return err;
}
#endif

static int clear_frame(uint32_t frame, void *user) {
    (void)user;
    unsigned phase = (frame / 60u) & 1u;
    int err = hel_frame_begin(phase ? 0.25f : 0.02f, 0.02f, 0.08f, 1.0f);
#if HEL_CORE_BOOT
    /* With no scene of its own to draw, this mode shows the engine's. */
    if (!err) {
        err = hel_core_draw();
    }
#endif
    if (!err) {
        err = hel_frame_present();
    }
    return err;
}

void hel_demo_main(void) {
    hel_log("HEL demo: start");
    hel_log_u32("HEL demo: mode", (uint32_t)HEL_DEMO_MODE);
    int err = hel_platform_init();
    if (err) {
        hel_log_u32("HEL demo: platform init failed", (uint32_t)err);
        return;
    }
    /* The heap comes before the C++ static constructors because a
       constructor in the transpiled core may allocate. */
    err = hel_heap_init(HEL_HEAP_BYTES);
    if (err) {
        hel_log_u32("HEL demo: heap init failed", (uint32_t)err);
        return;
    }
    hel_cxx_init();
    if (hel_cxx_selftest() != 0) {
        hel_log("HEL demo: C++ self-test failed, not submitting any frames");
        hel_heap_shutdown();
        return;
    }
    if (hel_matrix_test() != 0) {
        hel_log("HEL demo: matrix convention check failed, not submitting any frames");
        hel_heap_shutdown();
        return;
    }
    if (hel_endian_test() != 0) {
        hel_log("HEL demo: byte-order check failed, not submitting any frames");
        hel_heap_shutdown();
        return;
    }
#if defined(HEL_DEMO_INVALID_CONFIG)
    HelVideoConfig config = {1284u, 720u};
#else
    HelVideoConfig config = {1280u, 720u};
#endif
    err = hel_video_init(&config);
    if (err) {
        hel_log_u32("HEL demo: video init failed", (uint32_t)err);
        return;
    }
    HelFrameFn frame = clear_frame;
#if HEL_DEMO_MODE == HEL_DEMO_MODE_TRIANGLE
    err = hel_demo_triangle_init();
    frame = hel_demo_triangle_frame;
#elif HEL_DEMO_MODE == HEL_DEMO_MODE_CUBE
    err = hel_demo_cube_init();
    frame = hel_demo_cube_frame;
#endif
    if (err) {
        hel_log_u32("HEL demo: scene init failed", (uint32_t)err);
        hel_video_shutdown();
        return;
    }
#if HEL_CORE_BOOT
    err = hel_core_boot();
    if (err) {
        hel_log_u32("HEL demo: core boot failed", (uint32_t)err);
        hel_video_shutdown();
        hel_heap_shutdown();
        return;
    }
    g_scene_frame = frame;
    frame = core_and_scene_frame;
#endif
    err = hel_frame_run(frame, 0, HEL_DEMO_FRAME_LIMIT);
    hel_log_u32("HEL demo: frame loop stopped with", (uint32_t)err);
#if HEL_CORE_BOOT
    hel_core_shutdown();
#endif
    hel_log_u32("HEL demo: allocations before shutdown",
                hel_platform_allocation_count());
    hel_video_shutdown();
    hel_heap_report("HEL demo: heap at shutdown");
    hel_heap_shutdown();
    hel_log_u32("HEL demo: allocations after shutdown",
                hel_platform_allocation_count());
}
