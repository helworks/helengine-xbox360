/*
 * Engine math: column-major 4x4 matrices with column vectors.
 *
 * Conventions (fixed by the rendering plan): MVP = P * V * M; right-handed
 * view space looking along -Z with +Y up; hel_perspective maps the view
 * depth range [near, far] to clip depth [0, 1] after the perspective
 * divide (Direct3D style), which is what the backend's viewport expects.
 * Storage: m[column * 4 + row]. No libm dependency: the trigonometric
 * functions are implemented here so the guest and the host tests share them.
 */
#ifndef HEL_ENGINE_MATH_H
#define HEL_ENGINE_MATH_H

#include "frame.h"

#define HEL_PI 3.14159265358979323846f

HelMat4 hel_identity(void);
HelMat4 hel_mul(HelMat4 a, HelMat4 b);
HelMat4 hel_translate(float x, float y, float z);
HelMat4 hel_rotate_y(float radians);
HelMat4 hel_perspective(float fovy, float aspect, float near_z, float far_z);

typedef struct { float x, y, z, w; } HelVec4;
HelVec4 hel_transform(const HelMat4 *m, HelVec4 v);

float hel_sinf(float radians);
float hel_cosf(float radians);
float hel_tanf(float radians);
/* Wraps an angle into [0, 2*pi). */
float hel_wrap_angle(float radians);

#endif
