/* Host tests for src/demo/cube.c geometry (built by tools/test-host.ps1). */
#include <stdio.h>

#include "../src/demo/cube.h"

static int failures;

static void check(int condition, const char *what) {
    printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) failures++;
}

static void cross(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

int main(void) {
    check(HEL_CUBE_VERTEX_COUNT == 24 && HEL_CUBE_INDEX_COUNT == 36,
          "24 vertices and 36 indices");
    int in_bounds = 1;
    for (unsigned i = 0; i < HEL_CUBE_INDEX_COUNT; ++i) {
        if (hel_cube_indices[i] >= HEL_CUBE_VERTEX_COUNT) in_bounds = 0;
    }
    check(in_bounds, "every index is below the vertex count");

    int faces_ok = 1;
    for (unsigned face = 0; face < 6; ++face) {
        const uint16_t *idx = &hel_cube_indices[face * 6];
        /* Six indices per face referencing only that face's four vertices. */
        for (unsigned k = 0; k < 6; ++k) {
            if (idx[k] < face * 4 || idx[k] >= face * 4 + 4) faces_ok = 0;
        }
        /* Equal colour across the face. */
        const HelVertex *v0 = &hel_cube_vertices[face * 4];
        for (unsigned k = 1; k < 4; ++k) {
            const HelVertex *v = &hel_cube_vertices[face * 4 + k];
            if (v->r != v0->r || v->g != v0->g || v->b != v0->b || v->a != v0->a) {
                faces_ok = 0;
            }
        }
        /* Outward normals from both triangles' cross products. */
        float centre[3] = {0, 0, 0};
        for (unsigned k = 0; k < 4; ++k) {
            centre[0] += hel_cube_vertices[face * 4 + k].x * 0.25f;
            centre[1] += hel_cube_vertices[face * 4 + k].y * 0.25f;
            centre[2] += hel_cube_vertices[face * 4 + k].z * 0.25f;
        }
        for (unsigned tri = 0; tri < 2; ++tri) {
            const HelVertex *a = &hel_cube_vertices[idx[tri * 3 + 0]];
            const HelVertex *b = &hel_cube_vertices[idx[tri * 3 + 1]];
            const HelVertex *c = &hel_cube_vertices[idx[tri * 3 + 2]];
            float e1[3] = {b->x - a->x, b->y - a->y, b->z - a->z};
            float e2[3] = {c->x - a->x, c->y - a->y, c->z - a->z};
            float n[3];
            cross(e1, e2, n);
            float dot = n[0] * centre[0] + n[1] * centre[1] + n[2] * centre[2];
            if (dot <= 0.0f) faces_ok = 0; /* normal must point away from origin */
        }
    }
    check(faces_ok, "six indices per face, one colour per face, outward CCW winding");

    /* Face colour assignment. */
    check(hel_cube_vertices[0].r == 1 && hel_cube_vertices[0].g == 0 && hel_cube_vertices[0].b == 0, "+X is red");
    check(hel_cube_vertices[4].r == 0 && hel_cube_vertices[4].g == 1 && hel_cube_vertices[4].b == 1, "-X is cyan");
    check(hel_cube_vertices[8].r == 0 && hel_cube_vertices[8].g == 1 && hel_cube_vertices[8].b == 0, "+Y is green");
    check(hel_cube_vertices[12].r == 1 && hel_cube_vertices[12].g == 0 && hel_cube_vertices[12].b == 1, "-Y is magenta");
    check(hel_cube_vertices[16].r == 0 && hel_cube_vertices[16].g == 0 && hel_cube_vertices[16].b == 1, "+Z is blue");
    check(hel_cube_vertices[20].r == 1 && hel_cube_vertices[20].g == 1 && hel_cube_vertices[20].b == 0, "-Z is yellow");

    /* Extents. */
    int extents_ok = 1;
    for (unsigned i = 0; i < HEL_CUBE_VERTEX_COUNT; ++i) {
        const HelVertex *v = &hel_cube_vertices[i];
        if ((v->x != 0.5f && v->x != -0.5f) || (v->y != 0.5f && v->y != -0.5f) ||
            (v->z != 0.5f && v->z != -0.5f)) {
            extents_ok = 0;
        }
    }
    check(extents_ok, "all vertices at +/-0.5 extents");

    /* Angle wrapping: 4*pi seconds is exactly one turn. */
    float a0 = hel_cube_angle(0, 1000000);
    float a1 = hel_cube_angle(3141593, 1000000); /* pi seconds -> pi/2 */
    float a2 = hel_cube_angle((uint64_t)12566370 * 1000ull + 3141593ull, 1000000);
    check(a0 == 0.0f, "angle at t=0 is 0");
    check(a1 > 1.5705f && a1 < 1.5711f, "angle at t=pi s is pi/2");
    check(a2 > 1.5705f && a2 < 1.5711f, "angle wraps after 1000 turns");

    /* Camera: the +Z face centre (0,0,0.5) projects to the screen centre in
       front of the camera at any aspect. */
    HelMat4 mvp = hel_cube_mvp(16.0f / 9.0f, 0.0f);
    HelVec4 c = hel_transform(&mvp, (HelVec4){0, 0, 0.5f, 1});
    check(c.w > 0 && c.x == 0 && c.y == 0 && c.z / c.w > 0 && c.z / c.w < 1,
          "front face centre projects to the screen centre inside the depth range");

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
