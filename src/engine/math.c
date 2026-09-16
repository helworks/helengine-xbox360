#include "math.h"

HelMat4 hel_identity(void) {
    HelMat4 r = {{0}};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

HelMat4 hel_mul(HelMat4 a, HelMat4 b) {
    /* (a*b)[col j][row i] = sum_k a[col k][row i] * b[col j][row k] */
    HelMat4 r;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a.m[k * 4 + i] * b.m[j * 4 + k];
            }
            r.m[j * 4 + i] = s;
        }
    }
    return r;
}

HelMat4 hel_translate(float x, float y, float z) {
    HelMat4 r = hel_identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

HelMat4 hel_rotate_y(float radians) {
    /* Right-handed rotation about +Y: x' = x cos + z sin, z' = -x sin + z cos. */
    float c = hel_cosf(radians);
    float s = hel_sinf(radians);
    HelMat4 r = hel_identity();
    r.m[0] = c;   /* column 0, row 0 */
    r.m[2] = -s;  /* column 0, row 2 */
    r.m[8] = s;   /* column 2, row 0 */
    r.m[10] = c;  /* column 2, row 2 */
    return r;
}

HelMat4 hel_perspective(float fovy, float aspect, float near_z, float far_z) {
    float f = 1.0f / hel_tanf(fovy * 0.5f);
    HelMat4 r = {{0}};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = far_z / (near_z - far_z);
    r.m[11] = -1.0f;
    r.m[14] = (far_z * near_z) / (near_z - far_z);
    return r;
}

HelVec4 hel_transform(const HelMat4 *m, HelVec4 v) {
    HelVec4 r;
    r.x = m->m[0] * v.x + m->m[4] * v.y + m->m[8] * v.z + m->m[12] * v.w;
    r.y = m->m[1] * v.x + m->m[5] * v.y + m->m[9] * v.z + m->m[13] * v.w;
    r.z = m->m[2] * v.x + m->m[6] * v.y + m->m[10] * v.z + m->m[14] * v.w;
    r.w = m->m[3] * v.x + m->m[7] * v.y + m->m[11] * v.z + m->m[15] * v.w;
    return r;
}

float hel_wrap_angle(float radians) {
    const float two_pi = 2.0f * HEL_PI;
    while (radians >= two_pi) radians -= two_pi;
    while (radians < 0.0f) radians += two_pi;
    return radians;
}

/* sin on [-pi, pi] via a degree-11 Taylor series (error below 1e-6 after
   reduction to [-pi/2, pi/2]). */
static float sin_reduced(float x) {
    float x2 = x * x;
    float term = x;
    float sum = x;
    term *= -x2 / (2.0f * 3.0f); sum += term;
    term *= -x2 / (4.0f * 5.0f); sum += term;
    term *= -x2 / (6.0f * 7.0f); sum += term;
    term *= -x2 / (8.0f * 9.0f); sum += term;
    term *= -x2 / (10.0f * 11.0f); sum += term;
    return sum;
}

float hel_sinf(float radians) {
    float x = hel_wrap_angle(radians); /* [0, 2pi) */
    if (x > HEL_PI) x -= 2.0f * HEL_PI;  /* (-pi, pi] */
    /* Fold into [-pi/2, pi/2]: sin(pi - x) = sin(x). */
    if (x > HEL_PI * 0.5f) x = HEL_PI - x;
    else if (x < -HEL_PI * 0.5f) x = -HEL_PI - x;
    return sin_reduced(x);
}

float hel_cosf(float radians) { return hel_sinf(radians + HEL_PI * 0.5f); }

float hel_tanf(float radians) { return hel_sinf(radians) / hel_cosf(radians); }
