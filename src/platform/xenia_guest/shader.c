/*
 * Shader binaries for the unlit pipeline. The arrays are generated from the
 * shaders folder (unlit.vs.xasm, unlit.ps.xasm) by tools/xenos_asm.py at
 * build time into build/shaders.
 * Validation happens once before the first draw; emission uses
 * PM4_IM_LOAD_IMMEDIATE so the microcode travels inside the ring buffer
 * (docs/xenia-backend-contract.md section 3).
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"
#include "xenos_regs.h"

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
    err = validate_ucode("HEL shader: unlit pixel", hel_shader_unlit_ps,
                         HEL_SHADER_UNLIT_PS_DWORDS);
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

void hel_shaders_emit(void) {
    emit_shader(0u, hel_shader_unlit_vs, HEL_SHADER_UNLIT_VS_DWORDS);
    emit_shader(1u, hel_shader_unlit_ps, HEL_SHADER_UNLIT_PS_DWORDS);
    /* r0-r3 in the vertex shader, r0 in the pixel shader, one interpolator. */
    hel_ring_reg(XE_GPU_REG_SQ_PROGRAM_CNTL, 3u | (0u << 8) | (0u << 20));
    hel_ring_reg(XE_GPU_REG_SQ_CONTEXT_MISC, 0u);
    hel_ring_reg(XE_GPU_REG_SQ_INTERPOLATOR_CNTL, 0u);
    hel_ring_reg(XE_GPU_REG_SQ_VS_CONST, 0x000FF000u);
    hel_ring_reg(XE_GPU_REG_SQ_PS_CONST, 0x000FF100u);
}
