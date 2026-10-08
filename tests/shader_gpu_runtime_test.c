/* Host ring tests exercise the real shader.c with a controlled generated header. */
#include <stdio.h>
#include <string.h>

#define HEL_SHADER_GPU_PROBE 1
#define HEL_SHADER_GPU_PROBE_HEADER "../../../tests/shader_gpu_runtime_header.h"
#include "../src/platform/xenia_guest/shader.c"

#define RING_CAPACITY 4096u
static uint32_t ring_words[RING_CAPACITY];
static uint32_t ring_count;
static uint32_t ring_limit = RING_CAPACITY;
static int ring_overflow;
static uint32_t probe_id_logs;
static uint32_t probe_dword_logs;
static int failures;

void hel_ring_put(uint32_t value) {
    if (ring_count >= ring_limit) {
        ring_overflow = 1;
        return;
    }
    ring_words[ring_count++] = value;
}

void hel_ring_type3(uint32_t opcode, uint32_t count) {
    hel_ring_put(XE_PACKET_TYPE3(opcode, count));
}

void hel_ring_reg(uint32_t index, uint32_t value) {
    hel_ring_put(XE_PACKET_TYPE0(index, 1u));
    hel_ring_put(value);
}

int hel_ring_overflowed(void) { return ring_overflow; }

void hel_log(const char *message) {
    if (strcmp(message, "HEL shader probe: runtime-test-probe") == 0) {
        ++probe_id_logs;
    }
}

void hel_log_u32(const char *label, uint32_t value) {
    (void)value;
    if (strcmp(label, "HEL shader probe: vertex dwords") == 0 ||
        strcmp(label, "HEL shader probe: pixel dwords") == 0) {
        ++probe_dword_logs;
    }
}

static void check(int condition, const char *name) {
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

#ifndef HEL_TEST_INVALID_VS
static void ring_reset(void) {
    ring_count = 0;
    ring_limit = RING_CAPACITY;
    ring_overflow = 0;
}

static int find_program(uint32_t stage, const uint32_t *expected,
                        uint32_t dwords) {
    for (uint32_t i = 0; i + 2u + dwords < ring_count; ++i) {
        const uint32_t header = ring_words[i];
        if ((header & 0xC000FF00u) !=
                XE_PACKET_TYPE3(PM4_IM_LOAD_IMMEDIATE, 1u) ||
            ring_words[i + 1u] != stage ||
            ring_words[i + 2u] != dwords) {
            continue;
        }
        if (memcmp(&ring_words[i + 3u], expected,
                   dwords * sizeof(uint32_t)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int has_register(uint32_t index, uint32_t value) {
    for (uint32_t i = 0; i + 1u < ring_count; ++i) {
        if ((ring_words[i] & 0xFFFFu) == (index & 0x7FFFu) &&
            (ring_words[i] & 0xC0000000u) == 0u &&
            ring_words[i + 1u] == value) {
            return 1;
        }
    }
    return 0;
}

#ifndef HEL_TEST_ZERO_CONSTANTS
static int has_constant_packet(uint32_t physical_index,
                               const uint32_t *values, uint32_t count) {
    for (uint32_t i = 0; i + 1u + count < ring_count; ++i) {
        const uint32_t header = ring_words[i];
        if ((header & 0xC000FF00u) !=
                XE_PACKET_TYPE3(PM4_SET_CONSTANT, 1u) ||
            ring_words[i + 1u] != physical_index) {
            continue;
        }
        if (memcmp(&ring_words[i + 2u], values,
                   count * sizeof(uint32_t)) == 0) {
            return 1;
        }
    }
    return 0;
}
#endif

static uint32_t count_opcode(uint32_t opcode) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < ring_count; ++i) {
        if ((ring_words[i] & 0xC000FF00u) ==
            XE_PACKET_TYPE3(opcode, 1u)) {
            ++count;
        }
    }
    return count;
}
#endif

int main(void) {
#ifdef HEL_TEST_INVALID_VS
    check(!hel_shader_gpu_probe_is_active(), "probe starts inactive");
    check(hel_shader_gpu_probe_emit() == HEL_ERR_SHADER_INVALID,
          "invalid generated ucode is rejected");
    check(ring_count == 0u, "invalid generated ucode emits no packets");
    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
#else
    check(!hel_shader_gpu_probe_is_active(), "probe starts inactive");

    hel_shaders_frame_reset();
    ring_reset();
    hel_shaders_emit_kind(HEL_SHADER_UNLIT);
    const uint32_t legacy_upload_count = ring_count;
    check(legacy_upload_count != 0u, "inactive legacy shader emits normally");
    hel_shaders_emit_kind(HEL_SHADER_UNLIT);
    check(ring_count == legacy_upload_count,
          "inactive legacy shader keeps its normal emission cache");

    hel_shader_gpu_probe_set_active(1);
    check(hel_shader_gpu_probe_is_active(), "probe can be enabled");
    ring_reset();
    check(hel_shader_gpu_probe_emit() == HEL_OK,
          "generated vertex and pixel programs emit");
    check(find_program(0u, hel_probe_vs, HEL_PROBE_VS_DWORDS) >= 0,
          "vertex packet contains generated numeric dwords");
    check(find_program(1u, hel_probe_ps, HEL_PROBE_PS_DWORDS) >= 0,
          "pixel packet contains generated numeric dwords");
    check(has_register(XE_GPU_REG_SQ_PROGRAM_CNTL,
                       HEL_PROBE_VS_REGISTER_FIELD |
                           (HEL_PROBE_PS_REGISTER_FIELD << 8) |
                           (HEL_PROBE_INTERPOLATOR_FIELD << 20)),
          "SQ program register uses generated stage metadata");
    check(has_register(XE_GPU_REG_SQ_VS_CONST, 0x000FF000u) &&
              has_register(XE_GPU_REG_SQ_PS_CONST, 0x000FF100u),
          "probe preserves the legacy vertex and pixel constant banks");

    ring_reset();
    hel_shader_gpu_probe_constants();
#ifdef HEL_TEST_ZERO_CONSTANTS
    check(count_opcode(PM4_SET_CONSTANT) == 0u,
          "zero-count constant banks emit no packets");
#else
    check(count_opcode(PM4_SET_CONSTANT) == 2u,
          "both nonempty constant banks emit packets");
    check(has_constant_packet(0u, hel_probe_vs_constants,
                              HEL_PROBE_VS_CONSTANT_DWORDS),
          "vertex constants upload at physical c0 as numeric IEEE words");
    check(has_constant_packet(256u, hel_probe_ps_constants,
                              HEL_PROBE_PS_CONSTANT_DWORDS),
          "pixel constants upload at physical c256 as numeric IEEE words");
#endif

    /* A -> probe -> A must re-upload A after invalidation on both toggles. */
    hel_shader_gpu_probe_set_active(0);
    check(!hel_shader_gpu_probe_is_active(), "probe can be disabled");
    ring_reset();
    hel_shaders_emit_kind(HEL_SHADER_UNLIT);
    check(find_program(0u, hel_shader_unlit_vs,
                       HEL_SHADER_UNLIT_VS_DWORDS) >= 0,
          "legacy program A reloads after the probe");
    check(count_opcode(PM4_IM_LOAD_IMMEDIATE) == 2u,
          "legacy vertex and pixel programs both reload after the probe");

    check(probe_id_logs == 1u && probe_dword_logs == 2u,
          "probe identity and dword counts are reported once");

    ring_reset();
    ring_limit = 2u;
    check(hel_shader_gpu_probe_emit() == HEL_ERR_RING_FULL,
          "ring overflow is returned from probe emission");

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
#endif
}
