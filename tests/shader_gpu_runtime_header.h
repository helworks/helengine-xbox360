#ifndef HEL_SHADER_GPU_RUNTIME_HEADER_H
#define HEL_SHADER_GPU_RUNTIME_HEADER_H

#include <stdint.h>

static const uint32_t hel_probe_vs[] = {
#ifdef HEL_TEST_BAD_OPCODE
    0u, 0x00003000u, 0u
#else
    0u, 0x00001000u, 0u
#endif
};
static const uint32_t hel_probe_ps[] = {
    0u, 0x00001000u, 0u
};
static const uint32_t hel_probe_vs_constants[] = {
#ifdef HEL_TEST_ZERO_CONSTANTS
    0u
#else
    0x3F800000u, 0xC0200000u
#endif
};
static const uint32_t hel_probe_ps_constants[] = {
#ifdef HEL_TEST_ZERO_CONSTANTS
    0u
#else
    0x3E800000u, 0x3F400000u, 0x3F800000u
#endif
};

#define HEL_PROBE_VS_DWORDS 3u
#define HEL_PROBE_PS_DWORDS 3u
#ifdef HEL_TEST_ZERO_CONSTANTS
#define HEL_PROBE_VS_CONSTANT_DWORDS 0u
#define HEL_PROBE_PS_CONSTANT_DWORDS 0u
#else
#define HEL_PROBE_VS_CONSTANT_DWORDS 2u
#define HEL_PROBE_PS_CONSTANT_DWORDS 3u
#endif
#define HEL_PROBE_VS_REGISTER_FIELD 0x00000009u
#define HEL_PROBE_PS_REGISTER_FIELD 0x00000003u
#define HEL_PROBE_INTERPOLATOR_FIELD 0x00000002u
#define HEL_PROBE_UPLOAD_DWORDS 21u
#define HEL_PROBE_ID "runtime-test-probe"

#endif
