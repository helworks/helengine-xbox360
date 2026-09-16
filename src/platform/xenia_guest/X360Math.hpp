/*
 * Math functions for the transpiled engine core (codegen's
 * codegen-runtime-math-header hook, used because the core is generated with
 * codegen-use-std-math off).
 *
 * devkitPPC ships newlib's math library, which provides every function the
 * generated code calls, so this header just exposes it. libm is linked into
 * the guest image; its only external dependency is __errno, defined in
 * runtime.c. The one gap is the legacy name finite, which newlib does not
 * declare for this configuration: X360Math.cpp defines it in terms of the
 * compiler builtin.
 */
#pragma once

#include <math.h>

extern "C" int finite(double value);
