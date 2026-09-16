/*
 * Unit cube (extents +/-0.5) with one solid colour per face: 24 vertices
 * (four per face) and 36 indices, counter-clockwise when viewed from
 * outside each face. Colours: +X red, -X cyan, +Y green, -Y magenta,
 * +Z blue, -Z yellow. Camera at (0, 0, 3) looking at the origin.
 */
#ifndef HEL_DEMO_CUBE_H
#define HEL_DEMO_CUBE_H

#include <stdint.h>

#include "../engine/frame.h"
#include "../engine/math.h"

#define HEL_CUBE_VERTEX_COUNT 24u
#define HEL_CUBE_INDEX_COUNT 36u

extern const HelVertex hel_cube_vertices[HEL_CUBE_VERTEX_COUNT];
extern const uint16_t hel_cube_indices[HEL_CUBE_INDEX_COUNT];

/* Fixed camera: view translation (0,0,-3), vertical FOV pi/3, near 0.1,
   far 100; aspect from the caller. */
HelMat4 hel_cube_view_projection(float aspect);
/* Model-view-projection for a rotation about +Y by `radians`. */
HelMat4 hel_cube_mvp(float aspect, float radians);
/* Rotation angle in [0, 2*pi) for elapsed ticks at 0.5 rad/s, computed with
   integer wrapping so long runs keep precision. */
float hel_cube_angle(uint64_t elapsed_ticks, uint64_t tick_frequency);

#endif
