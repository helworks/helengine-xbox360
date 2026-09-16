/*
 * The one math symbol newlib does not declare for this target. The
 * generated core calls finite; the compiler builtin gives the same answer
 * without pulling in another library.
 */
#include "X360Math.hpp"

extern "C" int finite(double value) { return __builtin_isfinite(value) ? 1 : 0; }
