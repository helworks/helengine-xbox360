/*
 * Demo entry (called from the platform start code).
 *
 * Task 1 diagnostic frame: alternate two clear colours every 60 submitted
 * frames so a live presentation path is distinguishable from a stale image.
 * Building with -DHEL_DEMO_INVALID_CONFIG=1 requests an unsupported mode to
 * exercise the failure path (diagnostic logged, no GPU submission).
 */
#include <stdint.h>

#include "../engine/frame.h"
#include "../platform/xenia_guest/xenia_guest.h"

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
    err = hel_frame_run(clear_frame, 0, 0);
    hel_log_u32("HEL demo: frame loop stopped with", (uint32_t)err);
    hel_video_shutdown();
}
