/*
 * Converting the engine's matrices into the backend's.
 *
 * The engine uses row vectors: a point is a row and transforms are applied as
 * p' = p * M, so a composite is World * View * Projection. The backend's
 * HelMat4 uses column vectors, p' = M * p, so its composite is
 * Projection * View * World and its matrix is the transpose of the engine's.
 *
 * The transpose costs nothing here. float4x4 stores M11..M44 with the row
 * index first, and HelMat4 stores its 16 floats column-major, so the
 * transpose is the identity on storage: the engine's fields copied in
 * declaration order land in exactly the right slots. The copy is written out
 * field by field rather than memcpy'd, because float4x4 is a polymorphic
 * generated class and its members are not guaranteed to be contiguous.
 *
 * src/demo/matrix_test.cpp checks this against the backend's own perspective
 * and rotation on the console, so the claim is verified rather than reasoned.
 */
#pragma once

#include "float4x4.hpp"

extern "C" {
#include "../../engine/frame.h"
}

/// <summary>Converts an engine matrix to the backend's convention.</summary>
inline HelMat4 hel_matrix_from_engine(const ::float4x4 &source) {
    HelMat4 result;
    result.m[0] = source.M11;
    result.m[1] = source.M12;
    result.m[2] = source.M13;
    result.m[3] = source.M14;
    result.m[4] = source.M21;
    result.m[5] = source.M22;
    result.m[6] = source.M23;
    result.m[7] = source.M24;
    result.m[8] = source.M31;
    result.m[9] = source.M32;
    result.m[10] = source.M33;
    result.m[11] = source.M34;
    result.m[12] = source.M41;
    result.m[13] = source.M42;
    result.m[14] = source.M43;
    result.m[15] = source.M44;
    return result;
}
