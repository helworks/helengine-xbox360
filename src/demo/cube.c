#include "cube.h"


/* Each macro is one face's outward normal followed by its colour, which is
   the order HelVertex stores them in. The normal is per face, not per corner,
   because a cube has flat faces and sharing a corner normal would round them
   off. */
#define R 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
#define CY -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f
#define G 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f
#define MG 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f
#define B 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f
#define Y 0.0f, 0.0f, -1.0f, 1.0f, 1.0f, 0.0f, 1.0f

/* Each face lists its four corners counter-clockwise as seen from outside. */
const HelVertex hel_cube_vertices[HEL_CUBE_VERTEX_COUNT] = {
    /* +X (red): seen from +X, y right... corners around the face */
    {0.5f, -0.5f, 0.5f, R}, {0.5f, -0.5f, -0.5f, R},
    {0.5f, 0.5f, -0.5f, R}, {0.5f, 0.5f, 0.5f, R},
    /* -X (cyan) */
    {-0.5f, -0.5f, -0.5f, CY}, {-0.5f, -0.5f, 0.5f, CY},
    {-0.5f, 0.5f, 0.5f, CY}, {-0.5f, 0.5f, -0.5f, CY},
    /* +Y (green) */
    {-0.5f, 0.5f, 0.5f, G}, {0.5f, 0.5f, 0.5f, G},
    {0.5f, 0.5f, -0.5f, G}, {-0.5f, 0.5f, -0.5f, G},
    /* -Y (magenta) */
    {-0.5f, -0.5f, -0.5f, MG}, {0.5f, -0.5f, -0.5f, MG},
    {0.5f, -0.5f, 0.5f, MG}, {-0.5f, -0.5f, 0.5f, MG},
    /* +Z (blue) */
    {-0.5f, -0.5f, 0.5f, B}, {0.5f, -0.5f, 0.5f, B},
    {0.5f, 0.5f, 0.5f, B}, {-0.5f, 0.5f, 0.5f, B},
    /* -Z (yellow) */
    {0.5f, -0.5f, -0.5f, Y}, {-0.5f, -0.5f, -0.5f, Y},
    {-0.5f, 0.5f, -0.5f, Y}, {0.5f, 0.5f, -0.5f, Y},
};

const uint16_t hel_cube_indices[HEL_CUBE_INDEX_COUNT] = {
    0, 1, 2, 0, 2, 3,       /* +X */
    4, 5, 6, 4, 6, 7,       /* -X */
    8, 9, 10, 8, 10, 11,    /* +Y */
    12, 13, 14, 12, 14, 15, /* -Y */
    16, 17, 18, 16, 18, 19, /* +Z */
    20, 21, 22, 20, 22, 23, /* -Z */
};

HelMat4 hel_cube_view_projection(float aspect) {
    HelMat4 projection = hel_perspective(HEL_PI / 3.0f, aspect, 0.1f, 100.0f);
    HelMat4 view = hel_translate(0.0f, 0.0f, -3.0f);
    return hel_mul(projection, view);
}

HelMat4 hel_cube_mvp(float aspect, float radians) {
    return hel_mul(hel_cube_view_projection(aspect), hel_rotate_y(radians));
}

float hel_cube_angle(uint64_t elapsed_ticks, uint64_t tick_frequency) {
    /* 0.5 rad/s -> one turn every 4*pi seconds; wrap in integer ticks. */
    if (tick_frequency == 0) return 0.0f;
    uint64_t period = (uint64_t)((double)tick_frequency * 4.0 * (double)HEL_PI);
    if (period == 0) return 0.0f;
    uint64_t phase = elapsed_ticks % period;
    return (float)((double)phase / (double)period) * (2.0f * HEL_PI);
}
