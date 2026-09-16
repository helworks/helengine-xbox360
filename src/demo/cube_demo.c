/*
 * Rotating / fixed-angle cube demo mode: one indexed draw per frame with the
 * cube geometry from cube.c, time-based rotation about +Y at 0.5 rad/s, or a
 * fixed diagnostic angle when built with -DHEL_DEMO_FIXED_ANGLE=<0..3>
 * (0, pi/4, pi/2, pi radians).
 */
#include "../platform/xenia_guest/xenia_guest.h"
#include "cube.h"
#include "demo.h"

static HelMesh g_cube;
static uint64_t g_start_ticks;
static uint32_t g_logged_angles;

int hel_demo_cube_init(void) {
    g_start_ticks = hel_ticks();
    return hel_mesh_create(hel_cube_vertices, HEL_CUBE_VERTEX_COUNT,
                           hel_cube_indices, HEL_CUBE_INDEX_COUNT, &g_cube);
}

int hel_demo_cube_frame(uint32_t frame, void *user) {
    (void)frame;
    (void)user;
    float aspect = (float)hel_video_width() / (float)hel_video_height();
#if defined(HEL_DEMO_FIXED_ANGLE)
    static const float fixed_angles[4] = {0.0f, HEL_PI / 4.0f, HEL_PI / 2.0f, HEL_PI};
    float angle = fixed_angles[(HEL_DEMO_FIXED_ANGLE) & 3];
#else
    float angle = hel_cube_angle(hel_ticks() - g_start_ticks, hel_tick_frequency());
#endif
    if (g_logged_angles < 3 && (frame % 60u) == 0u) {
        hel_log_u32("HEL cube: angle milliradians", (uint32_t)(angle * 1000.0f));
        g_logged_angles++;
    }
    HelMat4 mvp = hel_cube_mvp(aspect, angle);
    int err = hel_frame_begin(0.02f, 0.02f, 0.08f, 1.0f);
    if (!err) err = hel_draw_unlit(g_cube, &mvp);
    if (!err) err = hel_frame_present();
    return err;
}
