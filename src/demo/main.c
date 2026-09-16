/*
 * Demo entry (called from the platform start code).
 *
 * HEL_DEMO_MODE selects the diagnostic: 0 alternates two clear colours every
 * 60 submitted frames, 1 draws the static unlit triangle, 2 the rotating
 * cube. Building with -DHEL_DEMO_INVALID_CONFIG=1 requests an unsupported
 * video mode to exercise the failure path (diagnostic logged, no submission).
 */
#include <stdint.h>

#include "../engine/frame.h"
#include "../platform/xenia_guest/xenia_guest.h"
#include "demo.h"

#ifndef HEL_DEMO_MODE
#define HEL_DEMO_MODE HEL_DEMO_MODE_CLEAR
#endif

static int clear_frame(uint32_t frame, void *user) {
    (void)user;
    unsigned phase = (frame / 60u) & 1u;
    int err = hel_frame_begin(phase ? 0.25f : 0.02f, 0.02f, 0.08f, 1.0f);
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
    err = hel_frame_run(frame, 0, 0);
    hel_log_u32("HEL demo: frame loop stopped with", (uint32_t)err);
    hel_video_shutdown();
}
