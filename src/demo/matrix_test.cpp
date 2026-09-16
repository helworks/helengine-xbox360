/*
 * Checks that the engine's matrices and the backend's agree.
 *
 * The renderer feeds engine-built matrices to a backend that uses the
 * opposite vector convention, so the conversion in X360EngineMath.hpp is
 * load-bearing: if it is wrong, geometry lands somewhere plausible but wrong
 * and nothing reports an error. Each check builds the same transform twice,
 * once with the engine's float4x4 and once with the backend's own math, and
 * compares the results element by element.
 *
 * Comparison uses a relative tolerance, because the two implementations
 * compute the same values by different routes and need not agree bit for
 * bit.
 */
#include "float4x4.hpp"
#include "float3.hpp"

#include "../platform/xenia_guest/X360EngineMath.hpp"

extern "C" {
#include "../engine/math.h"
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
}

namespace {

unsigned int g_failures;

void check(const char *what, bool ok) {
    hel_log_u32(what, ok ? 1u : 0u);
    if (!ok) {
        g_failures++;
    }
}

bool close_enough(float left, float right) {
    float difference = left - right;
    if (difference < 0.0f) {
        difference = -difference;
    }
    float scale = left < 0.0f ? -left : left;
    const float other = right < 0.0f ? -right : right;
    if (other > scale) {
        scale = other;
    }
    return difference <= 0.0005f * (scale > 1.0f ? scale : 1.0f);
}

bool matrices_match(const char *what, const HelMat4 &converted,
                    const HelMat4 &expected) {
    for (unsigned int index = 0; index < 16u; ++index) {
        if (!close_enough(converted.m[index], expected.m[index])) {
            hel_log(what);
            hel_log_u32("HEL matrix: first differing element", index);
            union { float f; unsigned int u; } got, want;
            got.f = converted.m[index];
            want.f = expected.m[index];
            hel_log_u32("HEL matrix: engine value bits", got.u);
            hel_log_u32("HEL matrix: backend value bits", want.u);
            return false;
        }
    }
    return true;
}

} // namespace

extern "C" unsigned int hel_matrix_test(void) {
    g_failures = 0;
    hel_log("HEL matrix: convention check start");

    /* Perspective: the engine's XNA-style right-handed projection with depth
       in [0,1] is the same matrix the backend builds. */
    const float fov = 1.04719755f; /* pi/3 */
    const float aspect = 1280.0f / 720.0f;
    ::float4x4 engineProjection;
    ::float4x4::CreatePerspectiveFieldOfView__out4(fov, aspect, 0.1f, 100.0f,
                                                   engineProjection);
    check("HEL matrix: perspective matches",
          matrices_match("HEL matrix: perspective",
                         hel_matrix_from_engine(engineProjection),
                         hel_perspective(fov, aspect, 0.1f, 100.0f)));

    /* Rotation about Y: catches a transpose that a symmetric matrix would
       hide. */
    ::float4x4 engineRotation;
    ::float4x4::CreateFromYawPitchRoll__out3(0.7f, 0.0f, 0.0f, engineRotation);
    check("HEL matrix: rotation about Y matches",
          matrices_match("HEL matrix: rotation",
                         hel_matrix_from_engine(engineRotation),
                         hel_rotate_y(0.7f)));

    /* Translation: catches the row/column mix-up directly, because the
       translation lives in the last row of one convention and the last
       column of the other. */
    ::float4x4 engineTranslation;
    ::float3 offset;
    offset.X = 1.0f;
    offset.Y = 2.0f;
    offset.Z = 3.0f;
    ::float4x4::CreateTranslation__ref0_out1(offset, engineTranslation);
    check("HEL matrix: translation matches",
          matrices_match("HEL matrix: translation",
                         hel_matrix_from_engine(engineTranslation),
                         hel_translate(1.0f, 2.0f, 3.0f)));

    /* Composition order: the engine multiplies world-then-view-then-
       projection with row vectors, the backend the other way round with
       column vectors. Both must describe the same transform. */
    ::float4x4 engineComposite;
    ::float4x4::Multiply__ref0_ref1_out2(engineTranslation, engineProjection,
                                         engineComposite);
    HelMat4 backendComposite =
        hel_mul(hel_perspective(fov, aspect, 0.1f, 100.0f),
                hel_translate(1.0f, 2.0f, 3.0f));
    check("HEL matrix: composition order matches",
          matrices_match("HEL matrix: composite",
                         hel_matrix_from_engine(engineComposite),
                         backendComposite));

    hel_log_u32("HEL matrix: failures", g_failures);
    return g_failures;
}
