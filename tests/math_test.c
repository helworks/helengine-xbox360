/* Host tests for src/engine/math.c (built by tools/test-host.ps1). */
#include <stdio.h>
#include <stdlib.h>

#include "../src/engine/math.h"

static int failures;

static void check(int condition, const char *what) {
    printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) failures++;
}

static int near(float a, float b) {
    float d = a - b;
    if (d < 0) d = -d;
    return d <= 0.00001f;
}

static int mat_equal(HelMat4 a, HelMat4 b) {
    for (int i = 0; i < 16; ++i) {
        if (!near(a.m[i], b.m[i])) return 0;
    }
    return 1;
}

int main(void) {
    HelMat4 t = hel_translate(1, 2, 3);
    check(mat_equal(hel_mul(hel_identity(), t), t),
          "identity * translation(1,2,3) == translation(1,2,3)");
    check(mat_equal(hel_mul(t, hel_identity()), t),
          "translation(1,2,3) * identity == translation(1,2,3)");

    HelVec4 p = {1, 0, 0, 1};
    HelMat4 ry = hel_rotate_y(HEL_PI / 2.0f);
    HelVec4 r = hel_transform(&ry, p);
    check(near(r.x, 0) && near(r.y, 0) && near(r.z, -1) && near(r.w, 1),
          "rotation_y(pi/2) * (1,0,0,1) == (0,0,-1,1)");

    HelMat4 ab = hel_mul(hel_translate(1, 0, 0), hel_rotate_y(HEL_PI / 2.0f));
    HelVec4 q = hel_transform(&ab, p);
    check(near(q.x, 1) && near(q.z, -1),
          "(T * R) * v applies R first then T (column vectors, MVP = P*V*M)");

    HelMat4 pr = hel_perspective(HEL_PI / 3.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    HelVec4 nearp = hel_transform(&pr, (HelVec4){0, 0, -0.1f, 1});
    HelVec4 farp = hel_transform(&pr, (HelVec4){0, 0, -100.0f, 1});
    check(near(nearp.z / nearp.w, 0.0f), "project(P, (0,0,-0.1,1)).z / w == 0");
    check(near(farp.z / farp.w, 1.0f), "project(P, (0,0,-100,1)).z / w == 1");
    check(near(pr.m[11], -1.0f) && near(pr.m[15], 0.0f), "perspective w = -z_view");
    check(near(pr.m[5], 1.0f / hel_tanf(HEL_PI / 6.0f)), "perspective m[5] == 1/tan(fovy/2)");
    check(near(pr.m[0], pr.m[5] * 9.0f / 16.0f), "perspective m[0] == f/aspect");

    check(near(hel_sinf(0), 0) && near(hel_sinf(HEL_PI / 2), 1) &&
              near(hel_sinf(HEL_PI), 0) && near(hel_sinf(-HEL_PI / 2), -1),
          "sin at 0, pi/2, pi, -pi/2");
    check(near(hel_cosf(0), 1) && near(hel_cosf(HEL_PI), -1) &&
              near(hel_cosf(HEL_PI / 3), 0.5f),
          "cos at 0, pi, pi/3");
    check(near(hel_tanf(HEL_PI / 4), 1), "tan(pi/4) == 1");
    check(near(hel_wrap_angle(7.0f * HEL_PI), HEL_PI), "wrap_angle(7pi) == pi");
    check(near(hel_sinf(100.0f), -0.50636564f), "sin(100 rad) after wrapping");

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
