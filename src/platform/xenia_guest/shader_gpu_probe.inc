/* Opt-in diagnostic program and constant uploads. This file is included by
   shader.c so it can reuse the legacy ucode validator and upload primitive. */
static int g_shader_gpu_probe_active;
static int g_shader_gpu_probe_validated;
static int g_shader_gpu_probe_reported;

void hel_shader_gpu_probe_set_active(int active) {
    const int enabled = active != 0;
    if (g_shader_gpu_probe_active != enabled) {
        g_shader_gpu_probe_active = enabled;
        /* The ring now contains a different program pair than the legacy
           cache remembers. Force the next ordinary draw to restore its pair. */
        g_emitted_valid = 0;
    }
}

int hel_shader_gpu_probe_is_active(void) {
    return g_shader_gpu_probe_active;
}

static int hel_shader_gpu_probe_validate(void) {
    if (g_shader_gpu_probe_validated) {
        return HEL_OK;
    }
    const struct {
        const char *name;
        const char *dword_label;
        const uint32_t *words;
        uint32_t count;
    } stages[] = {
        {"HEL shader probe vertex", "HEL shader probe: vertex dwords",
         hel_probe_vs, HEL_PROBE_VS_DWORDS},
        {"HEL shader probe pixel", "HEL shader probe: pixel dwords",
         hel_probe_ps, HEL_PROBE_PS_DWORDS}
    };
    for (uint32_t stage = 0; stage < 2u; ++stage) {
        const char *name = stages[stage].name;
        const uint32_t *words = stages[stage].words;
        const uint32_t count = stages[stage].count;
        if (count == 0u || count % 3u != 0u ||
            count > HEL_SHADER_MAX_DWORDS) {
            hel_log(name);
            hel_log_u32("HEL shader: bad dword count", count);
            return HEL_ERR_SHADER_INVALID;
        }
        const uint32_t first_address = words[0] & 0xFFFu;
        const uint32_t first_opcode = (words[1] >> 12) & 0xFu;
        if (first_opcode != 1u && first_opcode != 2u && first_opcode != 12u) {
            hel_log(name);
            hel_log_u32("HEL shader: unexpected first control-flow opcode",
                        first_opcode);
            return HEL_ERR_SHADER_INVALID;
        }
        if (first_opcode != 12u && first_address * 3u >= count) {
            hel_log(name);
            hel_log_u32("HEL shader: exec address outside the blob",
                        first_address);
            return HEL_ERR_SHADER_INVALID;
        }
        hel_log_u32(stages[stage].dword_label, count);
    }
    g_shader_gpu_probe_validated = 1;
    if (!g_shader_gpu_probe_reported) {
        g_shader_gpu_probe_reported = 1;
        hel_log("HEL shader probe: " HEL_PROBE_ID);
    }
    return HEL_OK;
}

int hel_shader_gpu_probe_emit(void) {
    int err = hel_shader_gpu_probe_validate();
    if (err) {
        return err;
    }

    emit_shader(0u, hel_probe_vs, HEL_PROBE_VS_DWORDS);
    emit_shader(1u, hel_probe_ps, HEL_PROBE_PS_DWORDS);
    hel_ring_reg(XE_GPU_REG_SQ_PROGRAM_CNTL,
                 HEL_PROBE_VS_REGISTER_FIELD |
                     (HEL_PROBE_PS_REGISTER_FIELD << 8) |
                     (HEL_PROBE_INTERPOLATOR_FIELD << 20));
    hel_ring_reg(XE_GPU_REG_SQ_CONTEXT_MISC, 0u);
    hel_ring_reg(XE_GPU_REG_SQ_INTERPOLATOR_CNTL, 0u);
    hel_ring_reg(XE_GPU_REG_SQ_VS_CONST, 0x000FF000u);
    hel_ring_reg(XE_GPU_REG_SQ_PS_CONST, 0x000FF100u);
    return hel_ring_overflowed() ? HEL_ERR_RING_FULL : HEL_OK;
}

static void hel_shader_gpu_probe_upload_constants(uint32_t physical_index,
                                                   const uint32_t *words,
                                                   uint32_t dwords) {
    if (dwords == 0u) {
        return;
    }
    hel_ring_type3(PM4_SET_CONSTANT, 1u + dwords);
    hel_ring_put(physical_index); /* type 0 (ALU), then the physical c index */
    for (uint32_t i = 0; i < dwords; ++i) {
        hel_ring_put(words[i]);
    }
}

void hel_shader_gpu_probe_constants(void) {
    hel_shader_gpu_probe_upload_constants(0u, hel_probe_vs_constants,
                                          HEL_PROBE_VS_CONSTANT_DWORDS);
    hel_shader_gpu_probe_upload_constants(256u, hel_probe_ps_constants,
                                          HEL_PROBE_PS_CONSTANT_DWORDS);
}
