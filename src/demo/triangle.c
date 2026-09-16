/*
 * Triangle diagnostic mode: one static triangle with red, green and blue
 * corners drawn with an identity transform over a dark clear.
 */
#include <stdint.h>

#include "../engine/frame.h"
#include "demo.h"

static const HelVertex triangle[] = {
    {-0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f},
    {0.5f, -0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f},
    {0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f},
};
static const uint16_t triangle_indices[] = {0, 1, 2};
/* Same vertices with an out-of-range index: must be rejected before upload. */
static const uint16_t bad_indices[] = {0, 1, 3};

static HelMesh g_triangle;

int hel_demo_triangle_init(void) {
    HelMesh rejected = 0;
    int err = hel_mesh_create(triangle, 3, bad_indices, 3, &rejected);
    if (err != HEL_ERR_MESH_INVALID || rejected != 0) {
        hel_log_u32("HEL demo: validator accepted a bad index, code", (uint32_t)err);
        return HEL_ERR_MESH_INVALID;
    }
    hel_log("HEL demo: validator rejected the out-of-range index as expected");
    return hel_mesh_create(triangle, 3, triangle_indices, 3, &g_triangle);
}

int hel_demo_triangle_frame(uint32_t frame, void *user) {
    (void)frame;
    (void)user;
    static const HelMat4 identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    int err = hel_frame_begin(0.02f, 0.02f, 0.08f, 1.0f);
    if (!err) err = hel_draw_unlit(g_triangle, &identity);
    if (!err) err = hel_frame_present();
    return err;
}
