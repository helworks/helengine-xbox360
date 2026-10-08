/*
 * Shader binaries. The arrays are generated from the shaders folder by
 * tools/xenos_asm.py at build time into build/shaders.
 *
 * Two vertex shaders are carried. The unlit one passes the vertex colour
 * through and is what the built-in demos draw with. The shaded one applies a
 * material colour and one directional light, and is what an authored scene
 * draws with. They share the pixel shader, which only emits interpolator 0.
 * Validation happens once before the first draw; emission uses
 * PM4_IM_LOAD_IMMEDIATE so the microcode travels inside the ring buffer
 * (docs/xenia-backend-contract.md section 3).
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"
#include "xenos_regs.h"

#include "shaded_vs.h"
#include "textured_ps.h"
#include "textured_vs.h"
#include "unlit_ps.h"
#include "unlit_vs.h"

#define HEL_SHADER_MAX_DWORDS 0x3FFFu /* PM4 type-3 count limit minus header */

static int g_validated;

static int validate_ucode(const char *name, const uint32_t *dwords,
                          uint32_t count) {
    if (count == 0 || count % 3u != 0 || count > HEL_SHADER_MAX_DWORDS) {
        hel_log(name);
        hel_log_u32("HEL shader: bad dword count", count);
        return HEL_ERR_SHADER_INVALID;
    }
    /* The first exec's address (word 0 bits 0-11 of control-flow entry 0)
       must point past the control-flow pairs and inside the blob. */
    uint32_t first_address = dwords[0] & 0xFFFu;
    uint32_t first_opcode = (dwords[1] >> 12) & 0xFu;
    if (first_opcode != 1u && first_opcode != 2u && first_opcode != 12u) {
        hel_log(name);
        hel_log_u32("HEL shader: unexpected first control-flow opcode",
                    first_opcode);
        return HEL_ERR_SHADER_INVALID;
    }
    if (first_opcode != 12u && (first_address * 3u >= count)) {
        hel_log(name);
        hel_log_u32("HEL shader: exec address outside the blob", first_address);
        return HEL_ERR_SHADER_INVALID;
    }
    hel_log(name);
    hel_log_u32("HEL shader: dwords", count);
    return HEL_OK;
}

int hel_shaders_validate(void) {
    if (g_validated) {
        return HEL_OK;
    }
    int err = validate_ucode("HEL shader: unlit vertex", hel_shader_unlit_vs,
                             HEL_SHADER_UNLIT_VS_DWORDS);
    if (err) return err;
    err = validate_ucode("HEL shader: shaded vertex", hel_shader_shaded_vs,
                         HEL_SHADER_SHADED_VS_DWORDS);
    if (err) return err;
    err = validate_ucode("HEL shader: textured vertex", hel_shader_textured_vs,
                         HEL_SHADER_TEXTURED_VS_DWORDS);
    if (err) return err;
    err = validate_ucode("HEL shader: unlit pixel", hel_shader_unlit_ps,
                         HEL_SHADER_UNLIT_PS_DWORDS);
    if (err) return err;
    err = validate_ucode("HEL shader: textured pixel", hel_shader_textured_ps,
                         HEL_SHADER_TEXTURED_PS_DWORDS);
    if (err) return err;
    g_validated = 1;
    return HEL_OK;
}

static void emit_shader(uint32_t shader_type, const uint32_t *dwords,
                        uint32_t count) {
    hel_ring_type3(PM4_IM_LOAD_IMMEDIATE, 2u + count);
    hel_ring_put(shader_type);
    hel_ring_put(count & 0xFFFFu); /* start 0 in the high half */
    for (uint32_t i = 0; i < count; ++i) {
        hel_ring_put(dwords[i]);
    }
}

/* Which program pair the ring last carried this frame. Re-uploading microcode
   before every draw is what overflowed a 64 KiB ring once an interface with
   dozens of batches arrived: a single shaded pair is 66 dwords, so fifty
   draws spend more than twenty thousand dwords saying the same thing. The
   pair is uploaded when it changes instead.

   Reset at the start of a frame rather than trusted across frames, because
   the ring is rewritten each frame and what the GPU still holds from the last
   one is not this code's to assume. */
static HelShaderKind g_emitted_kind;
static int g_emitted_valid;

void hel_shaders_frame_reset(void) { g_emitted_valid = 0; }

void hel_shaders_emit(void) { hel_shaders_emit_kind(HEL_SHADER_UNLIT); }

void hel_shaders_emit_kind(HelShaderKind kind) {
    if (g_emitted_valid && g_emitted_kind == kind) {
        return;
    }
    g_emitted_valid = 1;
    g_emitted_kind = kind;

    const uint32_t *vertex = hel_shader_unlit_vs;
    uint32_t vertex_dwords = HEL_SHADER_UNLIT_VS_DWORDS;
    const uint32_t *pixel = hel_shader_unlit_ps;
    uint32_t pixel_dwords = HEL_SHADER_UNLIT_PS_DWORDS;
    /* Highest register each program uses, and how many interpolators travel
       between them. A register count that is too low truncates the register
       file the shader was compiled against, which shows as geometry or
       colours going wrong rather than as a failure. */
    uint32_t vertex_registers = 3u;
    uint32_t pixel_registers = 0u;
    uint32_t interpolators = 0u;
    if (kind == HEL_SHADER_SHADED) {
        vertex = hel_shader_shaded_vs;
        vertex_dwords = HEL_SHADER_SHADED_VS_DWORDS;
        vertex_registers = 9u;
    } else if (kind == HEL_SHADER_TEXTURED) {
        vertex = hel_shader_textured_vs;
        vertex_dwords = HEL_SHADER_TEXTURED_VS_DWORDS;
        pixel = hel_shader_textured_ps;
        pixel_dwords = HEL_SHADER_TEXTURED_PS_DWORDS;
        vertex_registers = 5u;
        pixel_registers = 2u;
        interpolators = 1u; /* two, encoded as the count minus one */
    }
    static int reported_textured;
    if (kind == HEL_SHADER_TEXTURED && !reported_textured) {
        reported_textured = 1;
        hel_log_u32("HEL shader: emitting the textured pair, vertex dwords",
                    vertex_dwords);
    }
    emit_shader(0u, vertex, vertex_dwords);
    emit_shader(1u, pixel, pixel_dwords);
    hel_ring_reg(XE_GPU_REG_SQ_PROGRAM_CNTL,
                 vertex_registers | (pixel_registers << 8) |
                     (interpolators << 20));
    hel_ring_reg(XE_GPU_REG_SQ_CONTEXT_MISC, 0u);
    hel_ring_reg(XE_GPU_REG_SQ_INTERPOLATOR_CNTL, 0u);
    hel_ring_reg(XE_GPU_REG_SQ_VS_CONST, 0x000FF000u);
    hel_ring_reg(XE_GPU_REG_SQ_PS_CONST, 0x000FF100u);
}

#if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE
#ifndef HEL_SHADER_GPU_PROBE_HEADER
#define HEL_SHADER_GPU_PROBE_HEADER "../../../build/shader-gpu-probe/probe_shaders.h"
#endif
#include HEL_SHADER_GPU_PROBE_HEADER
#include "shader_gpu_probe.inc"
#endif
