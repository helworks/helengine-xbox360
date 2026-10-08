#include "x360_shader_runtime.h"
#include "xenos_regs.h"
#include "x360_shader_artifact_sha256.h"
#include <string.h>

static uint32_t field(HelX3spSpan record, uint32_t offset) {
    uint32_t value = 0;
    (void)hel_x3sp_span_u32(record, offset, &value);
    return value;
}

static int text_equals(const HelX3spView *view, HelX3spSpan record,
                       uint32_t offset, const char *expected) {
    HelX3spSpan text;
    if (!expected || hel_x3sp_string(view, field(record, offset), field(record, offset + 4), &text)) return 0;
    size_t length = strlen(expected);
    return length == text.size && (!length || memcmp(text.data, expected, length) == 0);
}

int hel_shader_program_init(const uint8_t *bytes, size_t size,
    const char *asset, const char *vertex, const char *pixel, const char *variant,
    uint32_t profile, HelShaderProgram *out) {
    HelShaderProgram pending = {0};
    HelX3spSpan pair, stage;
    if (!out) return HEL_X3SP_ERROR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!asset || !vertex || !pixel || !variant || (profile != 1 && profile != 2)) return HEL_X3SP_ERROR_ARGUMENT;
    int error = hel_x3sp_parse(bytes, size, &pending.view, 0);
    if (error) return error;
    (void)hel_x3sp_record(&pending.view, 1, 0, &pair);
    if (!text_equals(&pending.view, pair, 0, asset) || !text_equals(&pending.view, pair, 8, variant)) return HEL_X3SP_ERROR_SEMANTIC;
    for (uint32_t i = 0; i < 2; ++i) {
        (void)hel_x3sp_record(&pending.view, 2, i, &stage);
        if (!text_equals(&pending.view, stage, 72, i ? pixel : vertex)) return HEL_X3SP_ERROR_SEMANTIC;
        if (!i && field(stage, 4) != profile) return HEL_X3SP_ERROR_SEMANTIC;
        pending.words[i] = field(stage, 16);
        pending.constant_words[i] = field(stage, 48) * 4;
        pending.banks[i] = field(stage, 52);
    }
    pending.profile = profile;
    *out = pending;
    return 0;
}

int hel_shader_program_emit(const HelShaderProgram *program,
    const uint32_t *vs_constants, uint32_t vs_count,
    const uint32_t *ps_constants, uint32_t ps_count,
    uint32_t available, HelShaderWordSink sink, void *context) {
    if (!program || !program->view.data || !sink ||
        vs_count != program->constant_words[0] || ps_count != program->constant_words[1] ||
        (vs_count && !vs_constants) || (ps_count && !ps_constants)) return HEL_X3SP_ERROR_ARGUMENT;
    if (available < program->view.pair_upload_dwords) return HEL_X3SP_ERROR_RANGE;
    for (uint32_t i = 0; i < 2; ++i) {
        sink(context, XE_PACKET_TYPE3(PM4_IM_LOAD_IMMEDIATE, program->words[i] + 2));
        sink(context, i);
        sink(context, program->words[i]);
        for (uint32_t j = 0; j < program->words[i]; ++j) {
            uint32_t word;
            (void)hel_x3sp_word(&program->view, i, j, &word);
            sink(context, word);
        }
    }
    const uint32_t registers[] = { XE_GPU_REG_SQ_PROGRAM_CNTL, XE_GPU_REG_SQ_CONTEXT_MISC,
        XE_GPU_REG_SQ_INTERPOLATOR_CNTL, XE_GPU_REG_SQ_VS_CONST, XE_GPU_REG_SQ_PS_CONST };
    const uint32_t values[] = { program->view.program_control, 0, 0, 0x000FF000u, 0x000FF100u };
    for (uint32_t i = 0; i < 5; ++i) {
        sink(context, XE_PACKET_TYPE0(registers[i], 1));
        sink(context, values[i]);
    }
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t count = program->constant_words[i];
        const uint32_t *constants = i ? ps_constants : vs_constants;
        if (!count) continue;
        sink(context, XE_PACKET_TYPE3(PM4_SET_CONSTANT, count + 1));
        sink(context, program->banks[i]);
        for (uint32_t j = 0; j < count; ++j) sink(context, constants[j]);
    }
    return 0;
}

int hel_shader_program_projection(const HelShaderProgram *program,
    const float row_matrix[16], uint32_t constants[16]) {
    HelX3spSpan stage, used, declaration;
    if (!program || !program->view.data || !row_matrix || !constants) return HEL_X3SP_ERROR_ARGUMENT;
    if (program->constant_words[0] != 16 || program->constant_words[1] != 0) return HEL_X3SP_ERROR_UNSUPPORTED;
    (void)hel_x3sp_record(&program->view, 2, 0, &stage);
    if (field(stage, 60) != 1 || field(stage, 68) != 1) return HEL_X3SP_ERROR_UNSUPPORTED;
    (void)hel_x3sp_record(&program->view, 6, 0, &declaration);
    (void)hel_x3sp_record(&program->view, 7, 0, &used);
    if (!text_equals(&program->view, declaration, 0, "projection") || field(declaration, 8) != 5 ||
        field(declaration, 12) != 0 || field(used, 4) != 0 || field(used, 8) != 4) return HEL_X3SP_ERROR_UNSUPPORTED;
    uint32_t major = field(used, 12);
    /* shaderc represents HLSL's row-vector matrix as its SPIR-V transpose.
       RowMajor SPIR-V therefore stores columns of the Engine row matrix. */
    for (uint32_t row = 0; row < 4; ++row) {
        for (uint32_t column = 0; column < 4; ++column) {
            float value = row_matrix[major == 1 ? column * 4 + row : row * 4 + column];
            memcpy(&constants[row * 4 + column], &value, 4);
        }
    }
    return 0;
}

int hel_shader_mesh_path(const char *asset, const char *vertex, const char *pixel, char path[78]) {
    const char *strings[] = {asset, vertex, pixel, "MeshMaterialV1"};
    HelX3spSha256 hash;
    uint8_t digest[32];
    if (!path) return HEL_X3SP_ERROR_ARGUMENT;
    path[0] = 0;
    hel_x3sp_sha256_init(&hash);
    for (unsigned i = 0; i < 4; ++i) {
        if (!strings[i]) return HEL_X3SP_ERROR_ARGUMENT;
        size_t length = strlen(strings[i]);
        if (!length || length > 4096) return HEL_X3SP_ERROR_ARGUMENT;
        uint32_t remaining = (uint32_t)length;
        do {
            uint8_t byte = (uint8_t)(remaining & 127u);
            remaining >>= 7;
            if (remaining) byte |= 128u;
            hel_x3sp_sha256_update(&hash, &byte, 1);
        } while (remaining);
        hel_x3sp_sha256_update(&hash, (const uint8_t *)strings[i], length);
    }
    hel_x3sp_sha256_final(&hash, digest);
    memcpy(path, "shaders/", 8);
    for (unsigned i = 0; i < 32; ++i) {
        path[8 + i * 2] = "0123456789abcdef"[digest[i] >> 4];
        path[9 + i * 2] = "0123456789abcdef"[digest[i] & 15];
    }
    memcpy(path + 72, ".x3sp", 6);
    return 0;
}

int hel_shader_mesh_validate(const HelShaderProgram *program) {
    if (!program || !program->view.data) return HEL_X3SP_ERROR_ARGUMENT;
    if (program->profile != 1 || program->constant_words[0] != 16 || program->constant_words[1] != 4)
        return HEL_X3SP_ERROR_UNSUPPORTED;
    HelX3spSpan pair;
    (void)hel_x3sp_record(&program->view, 1, 0, &pair);
    if (!text_equals(&program->view, pair, 8, "MeshMaterialV1")) return HEL_X3SP_ERROR_UNSUPPORTED;
    for (uint32_t i = 0; i < 2; ++i) {
        HelX3spSpan stage, resource, declaration, used;
        (void)hel_x3sp_record(&program->view, 2, i, &stage);
        if (field(stage, 4) != 1 || field(stage, 40) != 1 || field(stage, 60) != 1 || field(stage, 68) != 1)
            return HEL_X3SP_ERROR_UNSUPPORTED;
        (void)hel_x3sp_record(&program->view, 5, i, &resource);
        (void)hel_x3sp_record(&program->view, 6, i, &declaration);
        (void)hel_x3sp_record(&program->view, 7, i, &used);
        if (field(resource, 0) != 1 || !text_equals(&program->view, resource, 4, i ? "Material" : "Transform") ||
            !text_equals(&program->view, declaration, 0, i ? "baseColor" : "mvp") ||
            field(declaration, 8) != (i ? 4u : 5u) || field(declaration, 12) != 0 ||
            field(used, 4) != 0 || field(used, 8) != (i ? 1u : 4u)) return HEL_X3SP_ERROR_UNSUPPORTED;
    }
    return 0;
}

int hel_shader_mesh_constants(const HelShaderProgram *program,
    const float row_matrix[16], uint32_t constants[16]) {
    if (!row_matrix || !constants) return HEL_X3SP_ERROR_ARGUMENT;
    int error = hel_shader_mesh_validate(program);
    if (error) return error;
    HelX3spSpan used;
    (void)hel_x3sp_record(&program->view, 7, 0, &used);
    uint32_t major = field(used, 12);
    /* Same HLSL row-vector / SPIR-V transpose contract as projection above. */
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column) {
            float value = row_matrix[major == 1 ? column * 4 + row : row * 4 + column];
            memcpy(&constants[row * 4 + column], &value, 4);
        }
    return 0;
}
