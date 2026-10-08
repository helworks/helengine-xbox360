#include "x360_shader_artifact.h"

#include "x360_shader_artifact_internal.h"

#include <string.h>

static const uint32_t record_sizes[8] = {
    48u, 160u, 4u, 36u, 40u, 24u, 24u, 1u
};

static int view_range_valid(const HelX3spView *view, size_t offset, size_t size)
{
    return view != NULL && view->data != NULL && offset <= view->size &&
           size <= view->size - offset;
}

int hel_x3sp_parse(const uint8_t *data, size_t size,
                   HelX3spView *out, HelX3spError *error)
{
    HelX3spEnvelope envelope;
    HelX3spView result;
    size_t i;
    int status;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (error != NULL) {
        error->code = HEL_X3SP_ERROR_OK;
        error->offset = 0;
    }
    if (out == NULL || data == NULL) {
        if (error != NULL)
            error->code = HEL_X3SP_ERROR_ARGUMENT;
        return HEL_X3SP_ERROR_ARGUMENT;
    }

    status = hel_x3sp_validate_envelope(data, size, &envelope, error);
    if (status != HEL_X3SP_ERROR_OK)
        return status;

    memset(&result, 0, sizeof result);
    result.data = data;
    result.size = size;
    memcpy(result.sections, envelope.sections, sizeof result.sections);
    status = hel_x3sp_validate_semantics(&result, error);
    if (status != HEL_X3SP_ERROR_OK)
        return status;

    for (i = 0; i < 8; ++i)
        out->sections[i] = result.sections[i];
    out->data = result.data;
    out->size = result.size;
    out->program_control = result.program_control;
    out->interpolator_count = result.interpolator_count;
    out->program_upload_dwords = result.program_upload_dwords;
    out->constant_upload_dwords = result.constant_upload_dwords;
    out->state_upload_dwords = result.state_upload_dwords;
    out->pair_upload_dwords = result.pair_upload_dwords;
    if (error != NULL) {
        error->code = HEL_X3SP_ERROR_OK;
        error->offset = 0;
    }
    return HEL_X3SP_ERROR_OK;
}

int hel_x3sp_record(const HelX3spView *view, uint32_t kind, uint32_t index,
                    HelX3spSpan *out)
{
    const HelX3spSection *section;
    size_t record_size;
    size_t local_offset;
    size_t section_offset;

    if (out != NULL) {
        out->data = NULL;
        out->size = 0;
    }
    if (out == NULL || view == NULL || view->data == NULL)
        return HEL_X3SP_ERROR_ARGUMENT;
    if (kind < 1u || kind > 8u)
        return HEL_X3SP_ERROR_RANGE;

    section = &view->sections[kind - 1u];
    record_size = record_sizes[kind - 1u];
    if (index >= section->count || section->length % record_size != 0 ||
        section->count > section->length / record_size)
        return HEL_X3SP_ERROR_RANGE;
    if ((size_t)index > SIZE_MAX / record_size)
        return HEL_X3SP_ERROR_RANGE;
    local_offset = (size_t)index * record_size;
    if (local_offset > section->length ||
        record_size > (size_t)section->length - local_offset)
        return HEL_X3SP_ERROR_RANGE;
    section_offset = section->offset;
    if (section_offset > view->size ||
        local_offset > view->size - section_offset ||
        record_size > view->size - section_offset - local_offset)
        return HEL_X3SP_ERROR_RANGE;

    out->data = view->data + section_offset + local_offset;
    out->size = record_size;
    return HEL_X3SP_ERROR_OK;
}

int hel_x3sp_string(const HelX3spView *view, uint32_t offset, uint32_t length,
                    HelX3spSpan *out)
{
    const HelX3spSection *section;
    size_t relative_offset;
    size_t absolute_offset;

    if (out != NULL) {
        out->data = NULL;
        out->size = 0;
    }
    if (out == NULL || view == NULL || view->data == NULL)
        return HEL_X3SP_ERROR_ARGUMENT;
    if (offset == 0u && length == 0u) {
        out->data = view->data;
        return HEL_X3SP_ERROR_OK;
    }
    if (length == 0u || length > 4096u)
        return HEL_X3SP_ERROR_RANGE;

    section = &view->sections[7];
    absolute_offset = (size_t)offset;
    if (absolute_offset < section->offset)
        return HEL_X3SP_ERROR_RANGE;
    relative_offset = absolute_offset - (size_t)section->offset;
    if (relative_offset > section->length ||
        (size_t)length > (size_t)section->length - relative_offset ||
        !view_range_valid(view, absolute_offset, (size_t)length))
        return HEL_X3SP_ERROR_RANGE;
    if (!hel_x3sp_validate_string_bytes(view->data + absolute_offset, length))
        return HEL_X3SP_ERROR_SEMANTIC;

    out->data = view->data + absolute_offset;
    out->size = length;
    return HEL_X3SP_ERROR_OK;
}

int hel_x3sp_word(const HelX3spView *view, uint32_t stage_index,
                  uint32_t word_index, uint32_t *out)
{
    HelX3spSpan stage;
    HelX3spSpan words;
    uint32_t code_offset;
    uint32_t word_count;
    size_t section_relative;
    size_t code_bytes;
    int status;

    if (out != NULL)
        *out = 0;
    if (out == NULL || view == NULL || view->data == NULL)
        return HEL_X3SP_ERROR_ARGUMENT;
    if (stage_index > 1u)
        return HEL_X3SP_ERROR_RANGE;
    status = hel_x3sp_record(view, 2u, stage_index, &stage);
    if (status != HEL_X3SP_ERROR_OK)
        return status;
    status = hel_x3sp_span_u32(stage, 12u, &code_offset);
    if (status != HEL_X3SP_ERROR_OK)
        return status;
    status = hel_x3sp_span_u32(stage, 16u, &word_count);
    if (status != HEL_X3SP_ERROR_OK)
        return status;
    if (word_index >= word_count ||
        word_count > view->sections[2].length / 4u)
        return HEL_X3SP_ERROR_RANGE;

    code_bytes = (size_t)word_count * 4u;
    if (code_offset < view->sections[2].offset)
        return HEL_X3SP_ERROR_RANGE;
    section_relative = (size_t)code_offset - view->sections[2].offset;
    if (section_relative > view->sections[2].length ||
        code_bytes > (size_t)view->sections[2].length - section_relative ||
        !view_range_valid(view, code_offset, code_bytes))
        return HEL_X3SP_ERROR_RANGE;
    words.data = view->data + code_offset;
    words.size = code_bytes;
    return hel_x3sp_span_u32(words, (size_t)word_index * 4u, out);
}

int hel_x3sp_span_u32(HelX3spSpan span, size_t offset, uint32_t *out)
{
    const uint8_t *p;

    if (out != NULL)
        *out = 0;
    if (out == NULL || span.data == NULL)
        return HEL_X3SP_ERROR_ARGUMENT;
    if (offset > span.size || 4u > span.size - offset)
        return HEL_X3SP_ERROR_RANGE;

    p = span.data + offset;
    *out = (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
    return HEL_X3SP_ERROR_OK;
}
