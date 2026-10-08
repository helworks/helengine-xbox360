#include "x360_shader_artifact_internal.h"

#include <string.h>

#define X3SP_STAGE_SIZE 160u
#define X3SP_INTERFACE_SIZE 36u
#define X3SP_RESOURCE_SIZE 40u
#define X3SP_DECLARATION_SIZE 24u
#define X3SP_ACCESS_SIZE 24u
#define X3SP_ABSENT UINT32_MAX

typedef struct X3spStageInfo {
    uint32_t stage_record;
    uint32_t profile;
    uint32_t registers;
    uint32_t code_offset;
    uint32_t code_count;
    uint32_t input_first;
    uint32_t input_count;
    uint32_t output_first;
    uint32_t output_count;
    uint32_t resource_first;
    uint32_t resource_count;
    uint32_t cbuffer_size;
    uint32_t cbuffer_capacity;
    uint32_t bank;
    uint32_t declaration_first;
    uint32_t declaration_count;
    uint32_t access_first;
    uint32_t access_count;
    uint32_t program_offset;
    uint32_t program_length;
    uint32_t entry_offset;
    uint32_t entry_length;
    uint32_t code_first;
} X3spStageInfo;

static int fail(HelX3spError *error, HelX3spErrorCode code, size_t offset)
{
    if (error != NULL) {
        error->code = (uint32_t)code;
        error->offset = (uint32_t)offset;
    }
    return (int)code;
}

static int read_u32(const HelX3spView *view, size_t offset, uint32_t *out,
                    HelX3spError *error)
{
    if (offset > view->size || 4u > view->size - offset)
        return fail(error, HEL_X3SP_ERROR_RANGE, offset);
    *out = hel_x3sp_read_le32(view->data + offset);
    return HEL_X3SP_ERROR_OK;
}

static size_t section_record_offset(const HelX3spView *view, uint32_t section,
                                    uint32_t index, uint32_t record_size)
{
    return (size_t)view->sections[section].offset +
           (size_t)index * (size_t)record_size;
}

static int check_partition_range(const HelX3spView *view, uint32_t section_index,
                                 uint32_t record_size, uint32_t offset,
                                 uint32_t count, uint32_t *cursor,
                                 uint32_t *first, HelX3spError *error,
                                 size_t field_offset)
{
    const HelX3spSection *section = &view->sections[section_index];
    size_t relative;
    uint32_t start;

    if (count == 0u) {
        *first = *cursor;
        if (offset != 0u)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, field_offset);
        return HEL_X3SP_ERROR_OK;
    }
    if ((size_t)offset < (size_t)section->offset)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
    relative = (size_t)offset - (size_t)section->offset;
    if (relative % record_size != 0u)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
    if (relative / record_size > section->count)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
    start = (uint32_t)(relative / record_size);
    if (count > section->count - start)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset + 4u);
    if (start != *cursor)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC, field_offset);
    *first = start;
    *cursor = start + count;
    return HEL_X3SP_ERROR_OK;
}

static int check_word_partition(const HelX3spView *view, uint32_t offset,
                                uint32_t count, uint32_t *cursor,
                                uint32_t *first, HelX3spError *error,
                                size_t field_offset)
{
    const HelX3spSection *section = &view->sections[2];
    size_t relative;
    uint32_t start;

    if (count == 0u)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset + 4u);
    if ((size_t)offset < (size_t)section->offset)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
    relative = (size_t)offset - (size_t)section->offset;
    if (relative % 4u != 0u)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
    if (relative / 4u > section->count)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
    start = (uint32_t)(relative / 4u);
    if (count > section->count - start)
        return fail(error, HEL_X3SP_ERROR_RANGE, field_offset + 4u);
    if (start != *cursor)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC, field_offset);
    *first = start;
    *cursor = start + count;
    return HEL_X3SP_ERROR_OK;
}

int hel_x3sp_validate_string_bytes(const uint8_t *data, size_t size)
{
    size_t i = 0;

    if (data == NULL && size != 0u)
        return 0;
    while (i < size) {
        uint8_t first = data[i++];
        uint8_t second;

        if (first == 0u)
            return 0;
        if (first <= 0x7fu)
            continue;
        if (first >= 0xc2u && first <= 0xdfu) {
            if (i >= size || data[i] < 0x80u || data[i] > 0xbfu)
                return 0;
            ++i;
            continue;
        }
        if (first >= 0xe0u && first <= 0xefu) {
            if (i + 1u >= size)
                return 0;
            second = data[i];
            if (first == 0xe0u) {
                if (second < 0xa0u || second > 0xbfu)
                    return 0;
            } else if (first == 0xedu) {
                if (second < 0x80u || second > 0x9fu)
                    return 0;
            } else if (second < 0x80u || second > 0xbfu) {
                return 0;
            }
            if (data[i + 1u] < 0x80u || data[i + 1u] > 0xbfu)
                return 0;
            i += 2u;
            continue;
        }
        if (first >= 0xf0u && first <= 0xf4u) {
            if (i + 2u >= size)
                return 0;
            second = data[i];
            if (first == 0xf0u) {
                if (second < 0x90u || second > 0xbfu)
                    return 0;
            } else if (first == 0xf4u) {
                if (second < 0x80u || second > 0x8fu)
                    return 0;
            } else if (second < 0x80u || second > 0xbfu) {
                return 0;
            }
            if (data[i + 1u] < 0x80u || data[i + 1u] > 0xbfu ||
                data[i + 2u] < 0x80u || data[i + 2u] > 0xbfu)
                return 0;
            i += 3u;
            continue;
        }
        return 0;
    }
    return 1;
}

static int get_string_reference(const HelX3spView *view,
                                const X3spStageInfo stages[2],
                                uint32_t ordinal, uint32_t *offset,
                                uint32_t *length)
{
    uint32_t pair_offset = view->sections[0].offset;
    uint32_t stage_index;
    uint32_t word_index;

    if (ordinal < 2u) {
        size_t field = (size_t)pair_offset + (size_t)ordinal * 8u;
        if (field > view->size || 8u > view->size - field)
            return 0;
        *offset = hel_x3sp_read_le32(view->data + field);
        *length = hel_x3sp_read_le32(view->data + field + 4u);
        return 1;
    }
    ordinal -= 2u;
    for (stage_index = 0; stage_index < 2u; ++stage_index) {
        size_t stage_record = stages[stage_index].stage_record;
        if (ordinal < 2u) {
            size_t field = stage_record + 72u + (size_t)ordinal * 8u;
            if (field > view->size || 8u > view->size - field)
                return 0;
            *offset = hel_x3sp_read_le32(view->data + field);
            *length = hel_x3sp_read_le32(view->data + field + 4u);
            return 1;
        }
        ordinal -= 2u;
        if (ordinal < stages[stage_index].resource_count) {
            word_index = stages[stage_index].resource_first + ordinal;
            size_t record = section_record_offset(view, 4u, word_index,
                                                  X3SP_RESOURCE_SIZE);
            if (record > view->size || 12u > view->size - record)
                return 0;
            *offset = hel_x3sp_read_le32(view->data + record + 4u);
            *length = hel_x3sp_read_le32(view->data + record + 8u);
            return 1;
        }
        ordinal -= stages[stage_index].resource_count;
        if (ordinal < stages[stage_index].declaration_count) {
            word_index = stages[stage_index].declaration_first + ordinal;
            size_t record = section_record_offset(view, 5u, word_index,
                                                  X3SP_DECLARATION_SIZE);
            if (record > view->size || 12u > view->size - record)
                return 0;
            *offset = hel_x3sp_read_le32(view->data + record);
            *length = hel_x3sp_read_le32(view->data + record + 4u);
            return 1;
        }
        ordinal -= stages[stage_index].declaration_count;
    }
    return 0;
}

static int validate_string_pool(const HelX3spView *view,
                                const X3spStageInfo stages[2],
                                HelX3spError *error)
{
    const HelX3spSection *pool = &view->sections[7];
    uint32_t ref_count = 2u;
    uint32_t stage_index;
    uint32_t ordinal;
    size_t next_new_string = pool->offset;
    size_t pool_end = (size_t)pool->offset + pool->length;

    for (stage_index = 0; stage_index < 2u; ++stage_index) {
        ref_count += 2u + stages[stage_index].resource_count +
                     stages[stage_index].declaration_count;
    }
    if (ref_count > 522u)
        return fail(error, HEL_X3SP_ERROR_RANGE, pool->offset);

    for (ordinal = 0; ordinal < ref_count; ++ordinal) {
        uint32_t offset, length, previous;
        int repeated = 0;
        size_t current_end;
        size_t field_offset = ordinal < 2u ?
            (size_t)view->sections[0].offset + (size_t)ordinal * 8u :
            (size_t)pool->offset;

        if (!get_string_reference(view, stages, ordinal, &offset, &length))
            return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
        if (length == 0u) {
            if (ordinal == 1u && offset == 0u)
                continue;
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, field_offset);
        }
        if (length > 4096u || (size_t)offset < (size_t)pool->offset)
            return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
        current_end = (size_t)offset + (size_t)length;
        if (current_end < (size_t)offset || current_end > pool_end)
            return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
        if (!hel_x3sp_validate_string_bytes(view->data + offset, length))
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, field_offset);

        for (previous = 0; previous < ordinal; ++previous) {
            uint32_t prior_offset, prior_length;
            if (!get_string_reference(view, stages, previous,
                                      &prior_offset, &prior_length))
                return fail(error, HEL_X3SP_ERROR_RANGE, field_offset);
            if (prior_length == 0u || prior_length != length)
                continue;
            if (memcmp(view->data + prior_offset, view->data + offset,
                       (size_t)length) == 0) {
                if (prior_offset != offset)
                    return fail(error, HEL_X3SP_ERROR_SEMANTIC, field_offset);
                repeated = 1;
                break;
            }
        }
        if (repeated)
            continue;
        if ((size_t)offset != next_new_string)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, field_offset);
        next_new_string = current_end;
    }
    if (next_new_string != pool_end)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC, pool->offset);
    return HEL_X3SP_ERROR_OK;
}

static int string_refs_equal(const HelX3spView *view, uint32_t left_offset,
                             uint32_t left_length, uint32_t right_offset,
                             uint32_t right_length)
{
    return left_length == right_length &&
           memcmp(view->data + left_offset, view->data + right_offset,
                  (size_t)left_length) == 0;
}

static int validate_unique_names(const HelX3spView *view, uint32_t section_index,
                                 uint32_t record_size, uint32_t first,
                                 uint32_t count, uint32_t name_offset,
                                 HelX3spError *error)
{
    uint32_t i;
    for (i = 0; i < count; ++i) {
        size_t current = section_record_offset(view, section_index,
                                              first + i, record_size);
        uint32_t current_offset = hel_x3sp_read_le32(view->data + current + name_offset);
        uint32_t current_length = hel_x3sp_read_le32(view->data + current + name_offset + 4u);
        uint32_t j;
        for (j = 0; j < i; ++j) {
            size_t prior = section_record_offset(view, section_index,
                                                 first + j, record_size);
            uint32_t prior_offset = hel_x3sp_read_le32(view->data + prior + name_offset);
            uint32_t prior_length = hel_x3sp_read_le32(view->data + prior + name_offset + 4u);
            if (string_refs_equal(view, current_offset, current_length,
                                  prior_offset, prior_length))
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, current + name_offset);
        }
    }
    return HEL_X3SP_ERROR_OK;
}

static int validate_stage_ranges(HelX3spView *view, X3spStageInfo stages[2],
                                 HelX3spError *error)
{
    uint32_t cursors[6] = {0, 0, 0, 0, 0, 0};
    uint32_t stage_index;

    for (stage_index = 0; stage_index < 2u; ++stage_index) {
        X3spStageInfo *stage = &stages[stage_index];
        size_t base = section_record_offset(view, 1u, stage_index,
                                            X3SP_STAGE_SIZE);
        uint32_t stage_tag, profile, registers;
        uint32_t code_offset, code_count;
        uint32_t input_offset, input_count, output_offset, output_count;
        uint32_t resource_offset, resource_count;
        uint32_t cbuffer_size, capacity, bank;
        uint32_t declaration_offset, declaration_count;
        uint32_t access_offset, access_count;
        uint32_t reserved0, reserved1;
        int status;

        stage->stage_record = (uint32_t)base;
        if (read_u32(view, base, &stage_tag, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 4u, &profile, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 8u, &registers, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 12u, &code_offset, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 16u, &code_count, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 20u, &input_offset, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 24u, &input_count, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 28u, &output_offset, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 32u, &output_count, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 36u, &resource_offset, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 40u, &resource_count, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 44u, &cbuffer_size, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 48u, &capacity, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 52u, &bank, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 56u, &declaration_offset, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 60u, &declaration_count, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 64u, &access_offset, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 68u, &access_count, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 152u, &reserved0, error) != HEL_X3SP_ERROR_OK ||
            read_u32(view, base + 156u, &reserved1, error) != HEL_X3SP_ERROR_OK)
            return (int)HEL_X3SP_ERROR_RANGE;

        if (stage_tag != stage_index + 1u)
            return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, base);
        if (profile != 1u && profile != 2u)
            return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, base + 4u);
        if (registers == 0u || registers > 64u)
            return fail(error, HEL_X3SP_ERROR_RANGE, base + 8u);
        if (code_count == 0u || code_count > 1024u || code_count % 3u != 0u)
            return fail(error, HEL_X3SP_ERROR_RANGE, base + 16u);
        if (reserved0 != 0u || reserved1 != 0u)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                        reserved0 != 0u ? base + 152u : base + 156u);

        stage->profile = profile;
        stage->registers = registers;
        stage->code_offset = code_offset;
        stage->code_count = code_count;
        stage->input_count = input_count;
        stage->output_count = output_count;
        stage->resource_count = resource_count;
        stage->cbuffer_size = cbuffer_size;
        stage->cbuffer_capacity = capacity;
        stage->bank = bank;
        stage->declaration_count = declaration_count;
        stage->access_count = access_count;
        stage->program_offset = hel_x3sp_read_le32(view->data + base + 72u);
        stage->program_length = hel_x3sp_read_le32(view->data + base + 76u);
        stage->entry_offset = hel_x3sp_read_le32(view->data + base + 80u);
        stage->entry_length = hel_x3sp_read_le32(view->data + base + 84u);

        status = check_word_partition(view, code_offset, code_count,
                                      &cursors[0], &stage->code_first,
                                      error, base + 12u);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = check_partition_range(view, 3u, X3SP_INTERFACE_SIZE,
                                       input_offset, input_count, &cursors[1],
                                       &stage->input_first, error, base + 20u);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = check_partition_range(view, 3u, X3SP_INTERFACE_SIZE,
                                       output_offset, output_count, &cursors[1],
                                       &stage->output_first, error, base + 28u);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = check_partition_range(view, 4u, X3SP_RESOURCE_SIZE,
                                       resource_offset, resource_count, &cursors[2],
                                       &stage->resource_first, error, base + 36u);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = check_partition_range(view, 5u, X3SP_DECLARATION_SIZE,
                                       declaration_offset, declaration_count,
                                       &cursors[3], &stage->declaration_first,
                                       error, base + 56u);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = check_partition_range(view, 6u, X3SP_ACCESS_SIZE,
                                       access_offset, access_count, &cursors[4],
                                       &stage->access_first, error, base + 64u);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
    }

    if (cursors[0] != view->sections[2].count ||
        cursors[1] != view->sections[3].count ||
        cursors[2] != view->sections[4].count ||
        cursors[3] != view->sections[5].count ||
        cursors[4] != view->sections[6].count)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                    view->sections[1].offset);
    return HEL_X3SP_ERROR_OK;
}

static int interface_record(const HelX3spView *view, uint32_t index,
                            uint32_t *kind, uint32_t *location,
                            uint32_t *width, uint32_t *slot,
                            uint32_t *fetch_constant, uint32_t *stride,
                            uint32_t *offset, uint32_t *format,
                            HelX3spError *error)
{
    size_t record = section_record_offset(view, 3u, index, X3SP_INTERFACE_SIZE);
    uint32_t reserved;
    uint32_t *fields[] = {kind, location, width, slot, fetch_constant,
                          stride, offset, format, &reserved};
    size_t i;

    for (i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
        int status = read_u32(view, record + i * 4u, fields[i], error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
    }
    if (reserved != 0u)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 32u);
    if (*format != 0u && *format != 2u && *format != 3u && *format != 4u)
        return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, record + 28u);
    return HEL_X3SP_ERROR_OK;
}

static int no_fetch(uint32_t slot, uint32_t fetch_constant, uint32_t stride,
                    uint32_t offset, uint32_t format)
{
    return slot == X3SP_ABSENT && fetch_constant == X3SP_ABSENT &&
           stride == 0u && offset == 0u && format == 0u;
}

static int validate_interface_sort(const HelX3spView *view, uint32_t first,
                                   uint32_t count, HelX3spError *error)
{
    uint32_t i;
    uint32_t prior_kind = 0u, prior_location = 0u;
    for (i = 0; i < count; ++i) {
        uint32_t kind, location, width, slot, constant, stride, offset, format;
        int status = interface_record(view, first + i, &kind, &location, &width,
                                      &slot, &constant, &stride, &offset,
                                      &format, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        if (kind != 1u && kind != 2u)
            return fail(error, HEL_X3SP_ERROR_UNSUPPORTED,
                        section_record_offset(view, 3u, first + i,
                                              X3SP_INTERFACE_SIZE));
        if (width < 1u || width > 4u)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                        section_record_offset(view, 3u, first + i,
                                              X3SP_INTERFACE_SIZE) + 8u);
        if (i != 0u && (kind < prior_kind ||
                       (kind == prior_kind && location <= prior_location)))
            return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                        section_record_offset(view, 3u, first + i,
                                              X3SP_INTERFACE_SIZE));
        prior_kind = kind;
        prior_location = location;
    }
    return HEL_X3SP_ERROR_OK;
}

static int validate_stage_interfaces(const HelX3spView *view,
                                     const X3spStageInfo stages[2],
                                     uint32_t stage_index,
                                     HelX3spError *error)
{
    const X3spStageInfo *stage = &stages[stage_index];
    uint32_t i;
    uint32_t position_count = 0u;
    uint32_t color_count = 0u;
    uint32_t varying_count = 0u;

    if (stage->input_count > 8u || stage->output_count > 9u)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                    stage->stage_record + (stage_index == 0u ? 24u : 32u));
    {
        int status = validate_interface_sort(view, stage->input_first,
                                             stage->input_count, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = validate_interface_sort(view, stage->output_first,
                                         stage->output_count, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
    }

    for (i = 0; i < stage->input_count; ++i) {
        uint32_t kind, location, width, slot, constant, stride, offset, format;
        size_t record;
        int status = interface_record(view, stage->input_first + i, &kind,
                                      &location, &width, &slot, &constant,
                                      &stride, &offset, &format, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        record = section_record_offset(view, 3u, stage->input_first + i,
                                       X3SP_INTERFACE_SIZE);
        if (kind != 1u)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record);
        if (stage_index == 0u) {
            uint32_t expected_width, expected_stride, expected_offset, expected_format;
            if (stage->profile == 1u) {
                static const uint32_t widths[4] = {3u, 3u, 2u, 4u};
                static const uint32_t offsets[4] = {0u, 3u, 6u, 8u};
                if (location >= 4u)
                    return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 4u);
                expected_width = widths[location];
                expected_stride = 12u;
                expected_offset = offsets[location];
                expected_format = expected_width;
            } else {
                if (location >= 6u)
                    return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 4u);
                expected_width = 4u;
                expected_stride = 24u;
                expected_offset = location * 4u;
                expected_format = 4u;
            }
            if (width != expected_width || slot != X3SP_ABSENT ||
                constant != 0u || stride != expected_stride ||
                offset != expected_offset || format != expected_format)
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 8u);
        } else if (location > 7u || slot != location ||
                   constant != X3SP_ABSENT || stride != 0u || offset != 0u ||
                   format != 0u) {
            /* PS inputs carry a varying slot but no vertex-fetch fields. */
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 4u);
        }
    }

    for (i = 0; i < stage->output_count; ++i) {
        uint32_t kind, location, width, slot, constant, stride, offset, format;
        size_t record;
        int status = interface_record(view, stage->output_first + i, &kind,
                                      &location, &width, &slot, &constant,
                                      &stride, &offset, &format, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        record = section_record_offset(view, 3u, stage->output_first + i,
                                       X3SP_INTERFACE_SIZE);
        if (stage_index == 0u) {
            if (kind == 2u) {
                ++position_count;
                if (location != 0u || width != 4u ||
                    !no_fetch(slot, constant, stride, offset, format))
                    return fail(error, HEL_X3SP_ERROR_SEMANTIC, record);
            } else {
                ++varying_count;
                if (location > 7u || slot != location ||
                    constant != X3SP_ABSENT || stride != 0u ||
                    offset != 0u || format != 0u)
                    return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 4u);
            }
        } else {
            ++color_count;
            if (kind != 1u || location != 0u || width != 4u ||
                !no_fetch(slot, constant, stride, offset, format))
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, record);
        }
    }
    if ((stage_index == 0u && position_count != 1u) ||
        (stage_index == 1u && color_count != 1u))
        return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                    stage->stage_record + (stage_index == 0u ? 32u : 32u));
    if (stage_index == 0u && varying_count > 8u)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                    stage->stage_record + 32u);
    return HEL_X3SP_ERROR_OK;
}

static int find_vs_varying(const HelX3spView *view,
                           const X3spStageInfo *vs, uint32_t location,
                           uint32_t *width, uint32_t *slot,
                           int *found,
                           HelX3spError *error)
{
    uint32_t i;
    *found = 0;
    for (i = 0; i < vs->output_count; ++i) {
        uint32_t kind, item_location, item_width, item_slot;
        uint32_t constant, stride, offset, format;
        int status = interface_record(view, vs->output_first + i, &kind,
                                      &item_location, &item_width, &item_slot,
                                      &constant, &stride, &offset, &format,
                                      error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        if (kind == 1u && item_location == location) {
            *width = item_width;
            *slot = item_slot;
            *found = 1;
            return HEL_X3SP_ERROR_OK;
        }
    }
    return HEL_X3SP_ERROR_OK;
}

static int validate_interface_link(const HelX3spView *view,
                                   const X3spStageInfo stages[2],
                                   HelX3spError *error)
{
    uint32_t i;
    for (i = 0; i < stages[1].input_count; ++i) {
        uint32_t kind, location, width, slot, constant, stride, offset, format;
        uint32_t vs_width = 0u, vs_slot = 0u;
        int found;
        size_t record = section_record_offset(view, 3u,
                                              stages[1].input_first + i,
                                              X3SP_INTERFACE_SIZE);
        int status = interface_record(view, stages[1].input_first + i, &kind,
                                      &location, &width, &slot, &constant,
                                      &stride, &offset, &format, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = find_vs_varying(view, &stages[0], location, &vs_width,
                                 &vs_slot, &found, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        if (!found || vs_width != width || vs_slot != slot)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 4u);
    }
    return HEL_X3SP_ERROR_OK;
}

static int validate_resources(const HelX3spView *view,
                              const X3spStageInfo stages[2],
                              uint32_t stage_index, uint32_t *constant_cost,
                              HelX3spError *error)
{
    const X3spStageInfo *stage = &stages[stage_index];
    uint32_t cbuffer_count = 0u, texture_count = 0u, sampler_count = 0u;
    uint32_t previous_type = 0u, previous_set = 0u, previous_slot = 0u;
    uint32_t i;
    size_t bank = stage_index == 0u ? 0u : 256u;
    int has_buffer;
    int status;

    status = validate_unique_names(view, 4u, X3SP_RESOURCE_SIZE,
                                   stage->resource_first, stage->resource_count,
                                   4u, error);
    if (status != HEL_X3SP_ERROR_OK)
        return status;
    for (i = 0; i < stage->resource_count; ++i) {
        uint32_t type, set, slot, physical_set, binding, fetch, resource_bank;
        uint32_t reserved;
        size_t record = section_record_offset(view, 4u,
                                              stage->resource_first + i,
                                              X3SP_RESOURCE_SIZE);
        type = hel_x3sp_read_le32(view->data + record);
        set = hel_x3sp_read_le32(view->data + record + 12u);
        slot = hel_x3sp_read_le32(view->data + record + 16u);
        physical_set = hel_x3sp_read_le32(view->data + record + 20u);
        binding = hel_x3sp_read_le32(view->data + record + 24u);
        fetch = hel_x3sp_read_le32(view->data + record + 28u);
        resource_bank = hel_x3sp_read_le32(view->data + record + 32u);
        reserved = hel_x3sp_read_le32(view->data + record + 36u);
        if (type < 1u || type > 3u)
            return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, record);
        if (set > UINT32_C(0x7fffffff) || slot > UINT32_C(0x7fffffff) ||
            physical_set > UINT32_C(0x7fffffff) ||
            binding > UINT32_C(0x7fffffff))
            return fail(error, HEL_X3SP_ERROR_RANGE, record + 12u);
        if (reserved != 0u)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 36u);
        if (i != 0u) {
            int out_of_order = type < previous_type;
            if (type == previous_type) {
                out_of_order = set < previous_set ||
                    (set == previous_set && slot <= previous_slot);
            }
            if (out_of_order)
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, record);
        }
        previous_type = type;
        previous_set = set;
        previous_slot = slot;

        if (type == 1u) {
            ++cbuffer_count;
            if (set != 0u || physical_set != 0u ||
                binding != 0u || fetch != X3SP_ABSENT || resource_bank != bank)
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 12u);
        } else {
            if (stage_index != 1u)
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, record);
            if (type == 2u)
                ++texture_count;
            else
                ++sampler_count;
            if (physical_set != set || fetch != 1u ||
                resource_bank != X3SP_ABSENT)
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 20u);
        }
    }
    if (cbuffer_count > 1u || texture_count > 1u || sampler_count > 1u ||
        texture_count != sampler_count)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                    stage->stage_record + 36u);

    has_buffer = cbuffer_count == 1u;
    if (!has_buffer) {
        if (stage->cbuffer_size != 0u || stage->cbuffer_capacity != 0u ||
            stage->bank != X3SP_ABSENT || stage->declaration_count != 0u ||
            stage->access_count != 0u)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                        stage->stage_record + 44u);
    } else {
        if (stage->cbuffer_size < 16u || stage->cbuffer_size > 4096u ||
            stage->cbuffer_size % 16u != 0u ||
            stage->cbuffer_capacity != stage->cbuffer_size / 16u ||
            stage->bank != bank)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                        stage->stage_record + 44u);
        *constant_cost += stage->cbuffer_capacity * 4u + 2u;
    }
    return HEL_X3SP_ERROR_OK;
}

static uint32_t expected_member_size(uint32_t type)
{
    static const uint8_t sizes[5] = {4u, 8u, 12u, 16u, 64u};
    return type >= 1u && type <= 5u ? sizes[type - 1u] : 0u;
}

static int validate_declarations(const HelX3spView *view,
                                 const X3spStageInfo *stage,
                                 HelX3spError *error)
{
    uint32_t prior_end = 0u;
    uint32_t i;
    int status = validate_unique_names(view, 5u, X3SP_DECLARATION_SIZE,
                                       stage->declaration_first,
                                       stage->declaration_count, 0u, error);
    if (status != HEL_X3SP_ERROR_OK)
        return status;
    if (stage->declaration_count > 256u || stage->access_count > 256u ||
        stage->access_count > stage->declaration_count)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                    stage->stage_record + 56u);
    for (i = 0; i < stage->declaration_count; ++i) {
        size_t record = section_record_offset(view, 5u,
                                              stage->declaration_first + i,
                                              X3SP_DECLARATION_SIZE);
        uint32_t type = hel_x3sp_read_le32(view->data + record + 8u);
        uint32_t offset = hel_x3sp_read_le32(view->data + record + 12u);
        uint32_t size = hel_x3sp_read_le32(view->data + record + 16u);
        uint32_t reserved = hel_x3sp_read_le32(view->data + record + 20u);
        uint32_t expected_size = expected_member_size(type);
        if (expected_size == 0u)
            return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, record + 8u);
        if (reserved != 0u || offset % 16u != 0u || size != expected_size ||
            offset > stage->cbuffer_size ||
            size > stage->cbuffer_size - offset ||
            (i != 0u && offset < prior_end))
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 12u);
        prior_end = offset + size;
    }
    return HEL_X3SP_ERROR_OK;
}

static int validate_accesses(const HelX3spView *view,
                             const X3spStageInfo stages[2],
                             uint32_t stage_index, HelX3spError *error)
{
    const X3spStageInfo *stage = &stages[stage_index];
    uint32_t previous_declaration = 0u;
    uint32_t i;

    for (i = 0; i < stage->access_count; ++i) {
        size_t record = section_record_offset(view, 6u,
                                              stage->access_first + i,
                                              X3SP_ACCESS_SIZE);
        uint32_t declaration_offset = hel_x3sp_read_le32(view->data + record);
        uint32_t first_register = hel_x3sp_read_le32(view->data + record + 4u);
        uint32_t register_count = hel_x3sp_read_le32(view->data + record + 8u);
        uint32_t major = hel_x3sp_read_le32(view->data + record + 12u);
        uint32_t matrix_stride = hel_x3sp_read_le32(view->data + record + 16u);
        uint32_t reserved = hel_x3sp_read_le32(view->data + record + 20u);
        uint32_t declaration_index;
        size_t declaration_record;
        uint32_t type, member_offset, expected_count;

        if (stage->declaration_count == 0u ||
            declaration_offset < view->sections[5].offset ||
            (declaration_offset - view->sections[5].offset) % X3SP_DECLARATION_SIZE != 0u)
            return fail(error, HEL_X3SP_ERROR_RANGE, record);
        declaration_index = (declaration_offset - view->sections[5].offset) /
                            X3SP_DECLARATION_SIZE;
        if (declaration_index < stage->declaration_first ||
            declaration_index - stage->declaration_first >= stage->declaration_count)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record);
        if (i != 0u && declaration_index <= previous_declaration)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record);
        previous_declaration = declaration_index;
        declaration_record = section_record_offset(view, 5u, declaration_index,
                                                   X3SP_DECLARATION_SIZE);
        type = hel_x3sp_read_le32(view->data + declaration_record + 8u);
        member_offset = hel_x3sp_read_le32(view->data + declaration_record + 12u);
        expected_count = type == 5u ? 4u : 1u;
        if (major > 2u)
            return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, record + 12u);
        if (first_register != member_offset / 16u ||
            register_count != expected_count ||
            first_register > stage->cbuffer_capacity ||
            register_count > stage->cbuffer_capacity - first_register ||
            reserved != 0u)
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 4u);
        if (type == 5u) {
            if ((major != 1u && major != 2u) || matrix_stride != 16u)
                return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 12u);
        } else if (major != 0u || matrix_stride != 0u) {
            return fail(error, HEL_X3SP_ERROR_SEMANTIC, record + 12u);
        }
    }
    return HEL_X3SP_ERROR_OK;
}

int hel_x3sp_validate_semantics(HelX3spView *view, HelX3spError *error)
{
    X3spStageInfo stages[2];
    uint32_t interface_cursor;
    uint32_t resource_cursor, declaration_cursor, access_cursor;
    uint32_t constant_cost = 0u;
    uint32_t highest_output = 0u;
    uint32_t has_user_output = 0u;
    uint32_t i;
    int status;

    if (view == NULL || view->data == NULL)
        return fail(error, HEL_X3SP_ERROR_ARGUMENT, 0u);
    if (view->sections[0].count != 1u ||
        view->sections[1].count != 2u)
        return fail(error, HEL_X3SP_ERROR_ENVELOPE, 20u);

    memset(stages, 0, sizeof stages);
    status = validate_stage_ranges(view, stages, error);
    if (status != HEL_X3SP_ERROR_OK)
        return status;

    interface_cursor = stages[1].output_first + stages[1].output_count;
    resource_cursor = stages[1].resource_first + stages[1].resource_count;
    declaration_cursor = stages[1].declaration_first + stages[1].declaration_count;
    access_cursor = stages[1].access_first + stages[1].access_count;
    if (interface_cursor != view->sections[3].count ||
        resource_cursor != view->sections[4].count ||
        declaration_cursor != view->sections[5].count ||
        access_cursor != view->sections[6].count)
        return fail(error, HEL_X3SP_ERROR_SEMANTIC,
                    view->sections[1].offset);

    status = validate_string_pool(view, stages, error);
    if (status != HEL_X3SP_ERROR_OK)
        return status;

    for (i = 0; i < 2u; ++i) {
        status = validate_stage_interfaces(view, stages, i, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = validate_resources(view, stages, i, &constant_cost, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = validate_declarations(view, &stages[i], error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
        status = validate_accesses(view, stages, i, error);
        if (status != HEL_X3SP_ERROR_OK)
            return status;
    }
    status = validate_interface_link(view, stages, error);
    if (status != HEL_X3SP_ERROR_OK)
        return status;

    for (i = 0; i < stages[0].output_count; ++i) {
        uint32_t kind = hel_x3sp_read_le32(view->data +
            section_record_offset(view, 3u, stages[0].output_first + i,
                                  X3SP_INTERFACE_SIZE));
        if (kind == 1u) {
            uint32_t location = hel_x3sp_read_le32(view->data +
                section_record_offset(view, 3u, stages[0].output_first + i,
                                      X3SP_INTERFACE_SIZE) + 4u);
            if (!has_user_output || location > highest_output)
                highest_output = location;
            has_user_output = 1u;
        }
    }
    view->program_control = (stages[0].registers - 1u) |
                            ((stages[1].registers - 1u) << 8) |
                            (highest_output << 20);
    view->interpolator_count = has_user_output ? highest_output + 1u : 0u;
    view->program_upload_dwords = stages[0].code_count +
                                  stages[1].code_count + 6u;
    view->constant_upload_dwords = constant_cost;
    view->state_upload_dwords = 10u;
    view->pair_upload_dwords = view->program_upload_dwords +
                               view->constant_upload_dwords +
                               view->state_upload_dwords;
    return HEL_X3SP_ERROR_OK;
}
