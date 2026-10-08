#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform/xenia_guest/x360_shader_artifact.h"
#include "../src/platform/xenia_guest/x360_shader_artifact_sha256.h"

static uint8_t golden[668];
static void repair_artifact_integrity(uint8_t *data, size_t size);
static size_t active_mutation_offset = SIZE_MAX;
static uint32_t active_mutation_value;

enum {
    TEST_PAIR = 192,
    TEST_STAGES = 240,
    TEST_STAGE_SIZE = 160,
    TEST_CODE = 560,
    TEST_INTERFACES = 584,
    TEST_RESOURCES = 872,
    TEST_DECLARATIONS = 1032,
    TEST_ACCESSES = 1104,
    TEST_ACCESS_SIZE = 24,
    TEST_STRINGS = 1176,
    TEST_RICH_CAPACITY = 2048
};

typedef struct TestStringEntry {
    uint32_t offset;
    uint32_t length;
} TestStringEntry;

static uint32_t span_u32_at(HelX3spSpan record, size_t offset);

static void read_fixture(void)
{
    FILE *file = fopen("tests/shaders/artifacts/minimal-v1.x3sp", "rb");
    assert(file != NULL);
    assert(fread(golden, 1, sizeof golden, file) == sizeof golden);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t intern_test_string(uint8_t *data, uint32_t *pool_length,
                                   TestStringEntry entries[16],
                                   uint32_t *entry_count,
                                   const uint8_t *bytes, uint32_t length)
{
    uint32_t i;
    for (i = 0; i < *entry_count; ++i) {
        if (entries[i].length == length &&
            memcmp(data + entries[i].offset, bytes, length) == 0)
            return entries[i].offset;
    }
    assert(*entry_count < 16u);
    entries[*entry_count].offset = TEST_STRINGS + *pool_length;
    entries[*entry_count].length = length;
    memcpy(data + entries[*entry_count].offset, bytes, length);
    *pool_length += length;
    return entries[(*entry_count)++].offset;
}

static uint32_t intern_ascii(uint8_t *data, uint32_t *pool_length,
                             TestStringEntry entries[16], uint32_t *entry_count,
                             const char *text)
{
    return intern_test_string(data, pool_length, entries, entry_count,
                              (const uint8_t *)text,
                              (uint32_t)strlen(text));
}

static void write_string_ref(uint8_t *record, size_t field,
                             uint32_t offset, uint32_t length)
{
    write_le32(record + field, offset);
    write_le32(record + field + 4u, length);
}

static void write_interface(uint8_t *data, uint32_t index,
                            uint32_t kind, uint32_t location, uint32_t width,
                            uint32_t slot, uint32_t fetch_constant,
                            uint32_t stride, uint32_t offset, uint32_t format)
{
    uint8_t *record = data + TEST_INTERFACES + (size_t)index * 36u;
    write_le32(record, kind);
    write_le32(record + 4u, location);
    write_le32(record + 8u, width);
    write_le32(record + 12u, slot);
    write_le32(record + 16u, fetch_constant);
    write_le32(record + 20u, stride);
    write_le32(record + 24u, offset);
    write_le32(record + 28u, format);
}

static void write_resource(uint8_t *data, uint32_t index, uint32_t type,
                           uint32_t name_offset, uint32_t name_length,
                           uint32_t logical_set, uint32_t logical_slot,
                           uint32_t physical_set, uint32_t binding,
                           uint32_t fetch_slot, uint32_t bank)
{
    uint8_t *record = data + TEST_RESOURCES + (size_t)index * 40u;
    write_le32(record, type);
    write_string_ref(record, 4u, name_offset, name_length);
    write_le32(record + 12u, logical_set);
    write_le32(record + 16u, logical_slot);
    write_le32(record + 20u, physical_set);
    write_le32(record + 24u, binding);
    write_le32(record + 28u, fetch_slot);
    write_le32(record + 32u, bank);
}

static void hash_stage_words(uint8_t *data, uint32_t stage_record,
                             uint32_t code_offset, uint32_t word_count)
{
    HelX3spSha256 ctx;
    uint8_t digest[32];
    uint32_t i;
    hel_x3sp_sha256_init(&ctx);
    for (i = 0; i < word_count; ++i) {
        uint32_t word = read_le32(data + code_offset + (size_t)i * 4u);
        uint8_t big_endian[4] = {
            (uint8_t)(word >> 24), (uint8_t)(word >> 16),
            (uint8_t)(word >> 8), (uint8_t)word
        };
        hel_x3sp_sha256_update(&ctx, big_endian, sizeof big_endian);
    }
    hel_x3sp_sha256_final(&ctx, digest);
    memcpy(data + stage_record + 120u, digest, sizeof digest);
}

static void set_directory(uint8_t *data, uint32_t index, uint32_t kind,
                          uint32_t offset, uint32_t length, uint32_t count)
{
    uint8_t *record = data + 64u + (size_t)index * 16u;
    write_le32(record, kind);
    write_le32(record + 4u, offset);
    write_le32(record + 8u, length);
    write_le32(record + 12u, count);
}

static size_t build_rich_fixture(uint8_t data[TEST_RICH_CAPACITY])
{
    static const uint8_t asset_id[] = {'a', 's', 's', 'e', 't', '-', 0xc2, 0xb5};
    static const uint8_t texture_name_bytes[] = {'a', 'l', 'b', 0xc3, 0xa9};
    static const uint32_t vertex_widths[4] = {3u, 3u, 2u, 4u};
    static const uint32_t vertex_offsets[4] = {0u, 3u, 6u, 8u};
    TestStringEntry entries[16];
    uint32_t entry_count = 0u, pool_length = 0u;
    uint32_t asset, variant, vs_name, main_name, vs_cb, matrix_name, color_name;
    uint32_t ps_name, ps_cb, texture_name, sampler_name, tint_name;
    uint32_t i;
    size_t file_size;
    uint8_t *vs, *ps;

    memset(data, 0, TEST_RICH_CAPACITY);
    asset = intern_test_string(data, &pool_length, entries, &entry_count,
                               asset_id, sizeof asset_id);
    variant = intern_ascii(data, &pool_length, entries, &entry_count, "v1");
    vs_name = intern_ascii(data, &pool_length, entries, &entry_count, "vs");
    main_name = intern_ascii(data, &pool_length, entries, &entry_count, "main");
    vs_cb = intern_ascii(data, &pool_length, entries, &entry_count, "vscb");
    matrix_name = intern_ascii(data, &pool_length, entries, &entry_count, "matrix");
    color_name = intern_ascii(data, &pool_length, entries, &entry_count, "color");
    ps_name = intern_ascii(data, &pool_length, entries, &entry_count, "ps");
    ps_cb = intern_ascii(data, &pool_length, entries, &entry_count, "pscb");
    texture_name = intern_test_string(data, &pool_length, entries, &entry_count,
                                      texture_name_bytes, sizeof texture_name_bytes);
    sampler_name = intern_ascii(data, &pool_length, entries, &entry_count, "samp");
    tint_name = intern_ascii(data, &pool_length, entries, &entry_count, "tint");

    memcpy(data, "X3SP", 4u);
    write_le32(data + 4u, 1u);
    write_le32(data + 8u, 64u);
    write_le32(data + 16u, 8u);
    write_le32(data + 20u, 8u);
    write_le32(data + 24u, 64u);
    set_directory(data, 0u, 1u, TEST_PAIR, 48u, 1u);
    set_directory(data, 1u, 2u, TEST_STAGES, 320u, 2u);
    set_directory(data, 2u, 3u, TEST_CODE, 24u, 6u);
    set_directory(data, 3u, 4u, TEST_INTERFACES, 288u, 8u);
    set_directory(data, 4u, 5u, TEST_RESOURCES, 160u, 4u);
    set_directory(data, 5u, 6u, TEST_DECLARATIONS, 72u, 3u);
    set_directory(data, 6u, 7u, TEST_ACCESSES, 72u, 3u);
    set_directory(data, 7u, 8u, TEST_STRINGS, pool_length, pool_length);
    write_string_ref(data + TEST_PAIR, 0u, asset, sizeof asset_id);
    write_string_ref(data + TEST_PAIR, 8u, variant, 2u);

    vs = data + TEST_STAGES;
    write_le32(vs, 1u);
    write_le32(vs + 4u, 1u);
    write_le32(vs + 8u, 2u);
    write_le32(vs + 12u, TEST_CODE);
    write_le32(vs + 16u, 3u);
    write_le32(vs + 20u, TEST_INTERFACES);
    write_le32(vs + 24u, 4u);
    write_le32(vs + 28u, TEST_INTERFACES + 4u * 36u);
    write_le32(vs + 32u, 2u);
    write_le32(vs + 36u, TEST_RESOURCES);
    write_le32(vs + 40u, 1u);
    write_le32(vs + 44u, 80u);
    write_le32(vs + 48u, 5u);
    write_le32(vs + 52u, 0u);
    write_le32(vs + 56u, TEST_DECLARATIONS);
    write_le32(vs + 60u, 2u);
    write_le32(vs + 64u, TEST_ACCESSES);
    write_le32(vs + 68u, 2u);
    write_string_ref(vs, 72u, vs_name, 2u);
    write_string_ref(vs, 80u, main_name, 4u);
    memset(vs + 88u, 0x11, 32u);

    ps = data + TEST_STAGES + TEST_STAGE_SIZE;
    write_le32(ps, 2u);
    write_le32(ps + 4u, 2u); /* PS provenance may differ from VS. */
    write_le32(ps + 8u, 1u);
    write_le32(ps + 12u, TEST_CODE + 12u);
    write_le32(ps + 16u, 3u);
    write_le32(ps + 20u, TEST_INTERFACES + 6u * 36u);
    write_le32(ps + 24u, 1u);
    write_le32(ps + 28u, TEST_INTERFACES + 7u * 36u);
    write_le32(ps + 32u, 1u);
    write_le32(ps + 36u, TEST_RESOURCES + 40u);
    write_le32(ps + 40u, 3u);
    write_le32(ps + 44u, 16u);
    write_le32(ps + 48u, 1u);
    write_le32(ps + 52u, 256u);
    write_le32(ps + 56u, TEST_DECLARATIONS + 48u);
    write_le32(ps + 60u, 1u);
    write_le32(ps + 64u, TEST_ACCESSES + 48u);
    write_le32(ps + 68u, 1u);
    write_string_ref(ps, 72u, ps_name, 2u);
    write_string_ref(ps, 80u, main_name, 4u);
    memset(ps + 88u, 0x22, 32u);

    for (i = 0; i < 4u; ++i) {
        write_interface(data, i, 1u, i, vertex_widths[i], UINT32_MAX, 0u,
                        12u, vertex_offsets[i], vertex_widths[i]);
    }
    write_interface(data, 4u, 1u, 0u, 4u, 0u, UINT32_MAX, 0u, 0u, 0u);
    write_interface(data, 5u, 2u, 0u, 4u, UINT32_MAX, UINT32_MAX,
                    0u, 0u, 0u);
    write_interface(data, 6u, 1u, 0u, 4u, 0u, UINT32_MAX, 0u, 0u, 0u);
    write_interface(data, 7u, 1u, 0u, 4u, UINT32_MAX, UINT32_MAX,
                    0u, 0u, 0u);

    write_resource(data, 0u, 1u, vs_cb, 4u, 0u, 17u, 0u, 0u, UINT32_MAX, 0u);
    write_resource(data, 1u, 1u, ps_cb, 4u, 0u, 300u, 0u, 0u, UINT32_MAX, 256u);
    write_resource(data, 2u, 2u, texture_name, (uint32_t)sizeof texture_name_bytes,
                   2u, 301u, 2u, 17u, 1u, UINT32_MAX);
    write_resource(data, 3u, 3u, sampler_name, 4u, 2u, 302u, 2u, 23u, 1u, UINT32_MAX);

    write_string_ref(data + TEST_DECLARATIONS, 0u, matrix_name, 6u);
    write_le32(data + TEST_DECLARATIONS + 8u, 5u);
    write_le32(data + TEST_DECLARATIONS + 12u, 0u);
    write_le32(data + TEST_DECLARATIONS + 16u, 64u);
    write_string_ref(data + TEST_DECLARATIONS + 24u, 0u, color_name, 5u);
    write_le32(data + TEST_DECLARATIONS + 32u, 4u);
    write_le32(data + TEST_DECLARATIONS + 36u, 64u);
    write_le32(data + TEST_DECLARATIONS + 40u, 16u);
    write_string_ref(data + TEST_DECLARATIONS + 48u, 0u, tint_name, 4u);
    write_le32(data + TEST_DECLARATIONS + 56u, 4u);
    write_le32(data + TEST_DECLARATIONS + 60u, 0u);
    write_le32(data + TEST_DECLARATIONS + 64u, 16u);

    write_le32(data + TEST_ACCESSES, TEST_DECLARATIONS);
    write_le32(data + TEST_ACCESSES + 4u, 0u);
    write_le32(data + TEST_ACCESSES + 8u, 4u);
    write_le32(data + TEST_ACCESSES + 12u, 1u);
    write_le32(data + TEST_ACCESSES + 16u, 16u);
    write_le32(data + TEST_ACCESSES + 24u, TEST_DECLARATIONS + 24u);
    write_le32(data + TEST_ACCESSES + 28u, 4u);
    write_le32(data + TEST_ACCESSES + 32u, 1u);
    write_le32(data + TEST_ACCESSES + 48u, TEST_DECLARATIONS + 48u);
    write_le32(data + TEST_ACCESSES + 52u, 0u);
    write_le32(data + TEST_ACCESSES + 56u, 1u);

    write_le32(data + TEST_CODE, UINT32_C(0x12345678));
    write_le32(data + TEST_CODE + 4u, UINT32_C(0x01020304));
    write_le32(data + TEST_CODE + 8u, UINT32_C(0xa0b0c0d0));
    write_le32(data + TEST_CODE + 12u, UINT32_C(0x87654321));
    write_le32(data + TEST_CODE + 16u, UINT32_C(0x04030201));
    write_le32(data + TEST_CODE + 20u, UINT32_C(0x0d0c0b0a));
    hash_stage_words(data, TEST_STAGES, TEST_CODE, 3u);
    hash_stage_words(data, TEST_STAGES + TEST_STAGE_SIZE,
                     TEST_CODE + 12u, 3u);

    file_size = (TEST_STRINGS + pool_length + 3u) & ~(size_t)3u;
    write_le32(data + 12u, (uint32_t)file_size);
    set_directory(data, 7u, 8u, TEST_STRINGS, pool_length, pool_length);
    repair_artifact_integrity(data, file_size);
    return file_size;
}

static void repair_artifact_integrity(uint8_t *data, size_t size)
{
    static const uint8_t zero_digest[32] = {0};
    HelX3spSha256 ctx;
    uint8_t digest[32];
    hel_x3sp_sha256_init(&ctx);
    hel_x3sp_sha256_update(&ctx, data, 32);
    hel_x3sp_sha256_update(&ctx, zero_digest, sizeof zero_digest);
    hel_x3sp_sha256_update(&ctx, data + 64, size - 64);
    hel_x3sp_sha256_final(&ctx, digest);
    memcpy(data + 32, digest, sizeof digest);
}

static void expect_parse_failure(const uint8_t *data, size_t size,
                                 uint32_t expected_error)
{
    HelX3spView view;
    HelX3spView cleared;
    HelX3spError error;
    uint8_t before[TEST_RICH_CAPACITY];
    int result;

    assert(size <= sizeof before);
    memcpy(before, data, size);
    memset(&view, 0xA5, sizeof view);
    memset(&cleared, 0, sizeof cleared);
    result = hel_x3sp_parse(data, size, &view, &error);
    if (result != (int)expected_error)
        fprintf(stderr, "parse failure mismatch: expected=%u got=%d code=%u offset=%u fixture_field=%zu value=%u\n",
                expected_error, result, error.code, error.offset,
                active_mutation_offset, active_mutation_value);
    assert(result == (int)expected_error);
    assert(error.code == expected_error);
    assert(memcmp(&view, &cleared, sizeof view) == 0);
    assert(memcmp(before, data, size) == 0);
}

static void expect_rich_field_failure(size_t field_offset, uint32_t value,
                                      uint32_t expected_error)
{
    uint8_t data[TEST_RICH_CAPACITY];
    size_t size = build_rich_fixture(data);
    active_mutation_offset = field_offset;
    active_mutation_value = value;
    write_le32(data + field_offset, value);
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, expected_error);
    active_mutation_offset = SIZE_MAX;
}

static void move_pool_after_shortened_access_table(uint8_t *data, size_t *size)
{
    const uint32_t old_pool_length = read_le32(data + 64u + 7u * 16u + 8u);
    const uint32_t shift = 24u;
    uint32_t i;

    /* Keep the VS matrix declaration but leave it unused; compact the access table. */
    memcpy(data + TEST_ACCESSES,
           data + TEST_ACCESSES + TEST_ACCESS_SIZE, TEST_ACCESS_SIZE);
    memcpy(data + TEST_ACCESSES + TEST_ACCESS_SIZE,
           data + TEST_ACCESSES + 2u * TEST_ACCESS_SIZE, TEST_ACCESS_SIZE);
    write_le32(data + TEST_STAGES + 64u, TEST_ACCESSES);
    write_le32(data + TEST_STAGES + 68u, 1u);
    write_le32(data + TEST_STAGES + TEST_STAGE_SIZE + 64u,
               TEST_ACCESSES + TEST_ACCESS_SIZE);
    write_le32(data + TEST_STAGES + TEST_STAGE_SIZE + 68u, 1u);
    set_directory(data, 6u, 7u, TEST_ACCESSES,
                  2u * TEST_ACCESS_SIZE, 2u);

    memmove(data + TEST_STRINGS - shift, data + TEST_STRINGS, old_pool_length);
    set_directory(data, 7u, 8u, TEST_STRINGS - shift,
                  old_pool_length, old_pool_length);
    for (i = 0; i < 2u; ++i) {
        uint32_t field = i * 8u;
        uint32_t offset = read_le32(data + TEST_PAIR + field);
        write_le32(data + TEST_PAIR + field, offset - shift);
    }
    for (i = 0; i < 2u; ++i) {
        size_t stage = TEST_STAGES + (size_t)i * TEST_STAGE_SIZE;
        uint32_t field;
        for (field = 72u; field <= 80u; field += 8u) {
            uint32_t offset = read_le32(data + stage + field);
            write_le32(data + stage + field, offset - shift);
        }
    }
    for (i = 0; i < 4u; ++i) {
        size_t record = TEST_RESOURCES + (size_t)i * 40u;
        uint32_t offset = read_le32(data + record + 4u);
        write_le32(data + record + 4u, offset - shift);
    }
    for (i = 0; i < 3u; ++i) {
        size_t record = TEST_DECLARATIONS + (size_t)i * 24u;
        uint32_t offset = read_le32(data + record);
        write_le32(data + record, offset - shift);
    }
    *size -= shift;
    memset(data + TEST_STRINGS - shift + old_pool_length, 0,
           *size - (TEST_STRINGS - shift + old_pool_length));
    write_le32(data + 12u, (uint32_t)*size);
    repair_artifact_integrity(data, *size);
}

static void make_sparse_unused_matrix_fixture(uint8_t *data, size_t *size)
{
    uint8_t old_interfaces[5u * 36u];
    uint32_t i;

    *size = build_rich_fixture(data);
    move_pool_after_shortened_access_table(data, size);
    write_le32(data + TEST_STAGES + 4u, 2u);
    write_le32(data + TEST_STAGES + 24u, 1u);
    write_le32(data + TEST_STAGES + 28u, TEST_INTERFACES + 36u);
    write_le32(data + TEST_STAGES + 32u, 5u);

    memcpy(old_interfaces, data + TEST_INTERFACES + 36u, sizeof old_interfaces);
    memcpy(data + TEST_INTERFACES + 36u,
           data + TEST_INTERFACES + 4u * 36u, 36u);
    write_interface(data, 0u, 1u, 5u, 4u, UINT32_MAX, 0u,
                    24u, 20u, 4u);
    for (i = 0; i < 3u; ++i) {
        uint32_t location = i + 1u;
        const uint8_t *input = old_interfaces + (size_t)i * 36u;
        uint32_t width = read_le32(input + 8u);
        write_interface(data, i + 2u, 1u, location, width, location,
                        UINT32_MAX, 0u, 0u, 0u);
    }
    repair_artifact_integrity(data, *size);
}

static void test_golden_parse_and_borrowed_accessors(void)
{
    uint8_t unaligned[sizeof golden + 1];
    uint8_t unchanged[sizeof golden + 1];
    uint8_t scalar_bytes[] = {0xa5, 0x78, 0x56, 0x34, 0x12};
    HelX3spView view;
    HelX3spError error;
    HelX3spSpan span;
    uint32_t value;

    unaligned[0] = 0x5a;
    memcpy(unaligned + 1, golden, sizeof golden);
    memcpy(unchanged, unaligned, sizeof unaligned);
    assert(hel_x3sp_parse(unaligned + 1, sizeof golden, &view, &error) == 0);
    assert(error.code == HEL_X3SP_ERROR_OK);
    assert(view.data == unaligned + 1);
    assert(view.size == sizeof golden);
    assert(view.sections[2].offset == 560);
    assert(view.program_control == 0);
    assert(view.interpolator_count == 0);
    assert(view.program_upload_dwords == 12);
    assert(view.constant_upload_dwords == 0);
    assert(view.state_upload_dwords == 10);
    assert(view.pair_upload_dwords == 22);

    assert(hel_x3sp_record(&view, 1, 0, &span) == 0);
    assert(span.data == view.data + 192 && span.size == 48);
    assert(hel_x3sp_record(&view, 3, 5, &span) == 0);
    assert(span.data == view.data + 580 && span.size == 4);
    assert(hel_x3sp_record(&view, 8, 10, &span) == 0);
    assert(span.data == view.data + 666 && span.size == 1 && span.data[0] == 'S');
    memset(&span, 0xA5, sizeof span);
    assert(hel_x3sp_record(&view, 3, 6, &span) != 0);
    assert(span.data == NULL && span.size == 0);

    assert(hel_x3sp_string(&view, 656, 7, &span) == 0);
    assert(span.size == 7 && memcmp(span.data, "fixture", 7) == 0);
    assert(hel_x3sp_string(&view, 666, 1, &span) == 0);
    assert(span.size == 1 && span.data[0] == 'S');
    assert(hel_x3sp_string(&view, 0, 0, &span) == 0);
    assert(span.size == 0);
    memset(&span, 0xA5, sizeof span);
    assert(hel_x3sp_string(&view, UINT32_MAX, 8, &span) != 0);
    assert(span.data == NULL && span.size == 0);
    assert(hel_x3sp_string(&view, 667, 1, &span) == HEL_X3SP_ERROR_RANGE);
    assert(span.data == NULL && span.size == 0);
    assert(hel_x3sp_string(&view, 656, 4097u, &span) == HEL_X3SP_ERROR_RANGE);
    assert(span.data == NULL && span.size == 0);
    assert(hel_x3sp_record(&view, 1u, 0u, NULL) == HEL_X3SP_ERROR_ARGUMENT);
    assert(hel_x3sp_string(&view, 656u, 1u, NULL) == HEL_X3SP_ERROR_ARGUMENT);
    assert(hel_x3sp_word(&view, 0u, 3u, &value) == HEL_X3SP_ERROR_RANGE);
    assert(value == 0u);
    assert(hel_x3sp_word(&view, 0u, 0u, NULL) == HEL_X3SP_ERROR_ARGUMENT);

    assert(hel_x3sp_word(&view, 0, 0, &value) == 0);
    assert(value == UINT32_C(0x12345678));
    assert(hel_x3sp_word(&view, 1, 2, &value) == 0);
    assert(value == UINT32_C(0x0d0c0b0a));
    value = UINT32_C(0xffffffff);
    assert(hel_x3sp_word(&view, 2, 0, &value) != 0);
    assert(value == 0);

    value = UINT32_C(0xffffffff);
    assert(hel_x3sp_span_u32((HelX3spSpan){scalar_bytes + 1, 4}, 0, &value) == 0);
    assert(value == UINT32_C(0x12345678));
    value = UINT32_C(0xffffffff);
    assert(hel_x3sp_span_u32((HelX3spSpan){scalar_bytes, 4}, SIZE_MAX, &value) != 0);
    assert(value == 0);
    assert(memcmp(unaligned, unchanged, sizeof unaligned) == 0);
}

static void test_rich_fixture_baseline(void)
{
    uint8_t data[TEST_RICH_CAPACITY];
    HelX3spView view;
    HelX3spError error;
    HelX3spSpan stage;
    HelX3spSpan asset;
    HelX3spSpan resource;
    HelX3spSpan name;
    uint32_t profile;
    size_t size = build_rich_fixture(data);
    int status = hel_x3sp_parse(data, size, &view, &error);

    assert(read_le32(data + TEST_RESOURCES + 16u) == 17u);
    assert(read_le32(data + TEST_RESOURCES + 40u + 16u) == 300u);

    if (status != HEL_X3SP_ERROR_OK)
        fprintf(stderr, "rich fixture rejected: code=%u offset=%u\n",
                error.code, error.offset);
    assert(status == HEL_X3SP_ERROR_OK);
    assert(view.program_control == 1u);
    assert(view.interpolator_count == 1u);
    assert(view.program_upload_dwords == 12u);
    assert(view.constant_upload_dwords == 28u);
    assert(view.state_upload_dwords == 10u);
    assert(view.pair_upload_dwords == 50u);
    assert(hel_x3sp_record(&view, 2u, 1u, &stage) == HEL_X3SP_ERROR_OK);
    assert(hel_x3sp_span_u32(stage, 4u, &profile) == HEL_X3SP_ERROR_OK);
    assert(profile == 2u);
    assert(hel_x3sp_record(&view, 5u, 2u, &resource) == HEL_X3SP_ERROR_OK);
    assert(span_u32_at(resource, 16u) == 301u);
    assert(span_u32_at(resource, 24u) == 17u);
    assert(hel_x3sp_string(&view, span_u32_at(resource, 4u), span_u32_at(resource, 8u), &name) == HEL_X3SP_ERROR_OK);
    assert(name.size == 5u && memcmp(name.data, "alb\xc3\xa9", 5u) == 0);
    assert(read_le32(data + TEST_STAGES + 80u) ==
           read_le32(data + TEST_STAGES + TEST_STAGE_SIZE + 80u));
    assert(hel_x3sp_string(&view, read_le32(data + TEST_PAIR), 8u,
                           &asset) == HEL_X3SP_ERROR_OK);
    assert(asset.size == 8u && asset.data[6] == 0xc2u && asset.data[7] == 0xb5u);
    assert(hel_x3sp_string(&view, read_le32(data + TEST_PAIR) + 6u, 1u,
                           &asset) == HEL_X3SP_ERROR_SEMANTIC);
    assert(asset.data == NULL && asset.size == 0u);
}

static void test_sparse_inputs_and_unused_matrix(void)
{
    uint8_t data[TEST_RICH_CAPACITY];
    HelX3spView view;
    HelX3spError error;
    size_t size;
    int status;

    make_sparse_unused_matrix_fixture(data, &size);
    status = hel_x3sp_parse(data, size, &view, &error);
    if (status != HEL_X3SP_ERROR_OK)
        fprintf(stderr, "sparse/unused fixture rejected: code=%u offset=%u\n",
                error.code, error.offset);
    assert(status == HEL_X3SP_ERROR_OK);
    assert(view.interpolator_count == 4u);
    assert(view.program_control == ((3u << 20) | 1u));
}

static void test_repaired_integrity_semantic_mutations(void)
{
    static const struct {
        size_t offset;
        uint32_t value;
        uint32_t error;
    } mutations[] = {
        {TEST_STAGES + 4u, 99u, HEL_X3SP_ERROR_UNSUPPORTED},
        {TEST_STAGES + 52u, 256u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_STAGES + 28u, TEST_RESOURCES, HEL_X3SP_ERROR_RANGE},
        {TEST_RESOURCES + 2u * 40u + 28u, 0u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_RESOURCES + 2u * 40u, 9u, HEL_X3SP_ERROR_UNSUPPORTED},
        {TEST_INTERFACES + 8u, 4u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_INTERFACES + 20u, 24u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_INTERFACES + 24u, 1u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_INTERFACES + 28u, 1u, HEL_X3SP_ERROR_UNSUPPORTED},
        {TEST_INTERFACES + 5u * 36u, 1u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_INTERFACES + 7u * 36u, 2u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_INTERFACES + 6u * 36u + 8u, 3u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_RESOURCES + 3u * 40u, 2u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_ACCESSES + 12u, 0u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_ACCESSES + 12u, 3u, HEL_X3SP_ERROR_UNSUPPORTED},
        {TEST_ACCESSES + 16u, 8u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_DECLARATIONS + 24u + 12u, 48u, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_ACCESSES + 24u, TEST_DECLARATIONS, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_ACCESSES + 2u * 24u, TEST_DECLARATIONS, HEL_X3SP_ERROR_SEMANTIC},
        {TEST_STAGES + TEST_STAGE_SIZE + 28u, 0u, HEL_X3SP_ERROR_RANGE},
        {TEST_STAGES + TEST_STAGE_SIZE + 32u, 0u, HEL_X3SP_ERROR_SEMANTIC}
    };
    uint8_t data[TEST_RICH_CAPACITY];
    size_t size;
    size_t i;

    for (i = 0; i < sizeof mutations / sizeof mutations[0]; ++i)
        expect_rich_field_failure(mutations[i].offset, mutations[i].value,
                                  mutations[i].error);

    size = build_rich_fixture(data);
    write_string_ref(data + TEST_RESOURCES + 3u * 40u, 4u,
                     read_le32(data + TEST_RESOURCES + 2u * 40u + 4u),
                     read_le32(data + TEST_RESOURCES + 2u * 40u + 8u));
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    data[TEST_STRINGS] = 0u;
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    data[TEST_STRINGS + 6u] = 0xc0u; /* Overlong lead byte. */
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    write_le32(data + TEST_STAGES + 72u, TEST_STRINGS + 1u);
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    write_le32(data + TEST_PAIR + 4u, 4097u);
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_RANGE);

    size = build_rich_fixture(data);
    {
        uint32_t texture_offset = read_le32(data + TEST_RESOURCES + 2u * 40u + 4u);
        data[texture_offset] = 0xedu;
        data[texture_offset + 1u] = 0xa0u; /* UTF-8 surrogate encoding. */
        data[texture_offset + 2u] = 0x80u;
    }
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    {
        uint32_t tint_offset = read_le32(data + TEST_DECLARATIONS + 48u);
        data[tint_offset] = 0xf4u;
        data[tint_offset + 1u] = 0x90u; /* Beyond U+10FFFF. */
        data[tint_offset + 2u] = 0x80u;
        data[tint_offset + 3u] = 0x80u;
    }
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    write_le32(data + TEST_STAGES + TEST_STAGE_SIZE + 28u, 0u);
    write_le32(data + TEST_STAGES + TEST_STAGE_SIZE + 32u, 0u);
    repair_artifact_integrity(data, size);
    expect_parse_failure(data, size, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    data[size] = 0x7fu;
    write_le32(data + 12u, (uint32_t)(size + 4u));
    write_le32(data + 64u + 7u * 16u + 8u,
               read_le32(data + 64u + 7u * 16u + 8u) + 4u);
    write_le32(data + 64u + 7u * 16u + 12u,
               read_le32(data + 64u + 7u * 16u + 12u) + 4u);
    repair_artifact_integrity(data, size + 4u);
    expect_parse_failure(data, size + 4u, HEL_X3SP_ERROR_SEMANTIC);

    size = build_rich_fixture(data);
    data[read_le32(data + TEST_PAIR + 8u)] = 0xc2u;
    data[read_le32(data + TEST_PAIR + 8u) + 1u] = 0x85u;
    repair_artifact_integrity(data, size);
    {
        HelX3spView view;
        HelX3spError error;
        assert(hel_x3sp_parse(data, size, &view, &error) == HEL_X3SP_ERROR_OK);
    }
}

static void test_semantic_reference_with_repaired_integrity(void)
{
    uint8_t mutated[sizeof golden];

    memcpy(mutated, golden, sizeof mutated);
    /* VS output now aliases the PS output record in the same table. */
    write_le32(mutated + 240 + 28, 620);
    repair_artifact_integrity(mutated, sizeof mutated);
    expect_parse_failure(mutated, sizeof mutated, HEL_X3SP_ERROR_SEMANTIC);
}

static void test_failure_clears_view_for_every_truncation(void)
{
    size_t size;

    for (size = 0; size < sizeof golden; ++size) {
        HelX3spView view;
        HelX3spView cleared;
        HelX3spError error;
        memset(&view, 0xA5, sizeof view);
        memset(&cleared, 0, sizeof cleared);
        assert(hel_x3sp_parse(golden, size, &view, &error) != HEL_X3SP_ERROR_OK);
        assert(memcmp(&view, &cleared, sizeof view) == 0);
    }
}

static uint32_t next_corpus_value(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void test_fixed_seed_bounded_mutation_corpus(void)
{
    uint32_t seed = UINT32_C(0x6d2b79f5);
    uint32_t iteration;
    for (iteration = 0u; iteration < 512u; ++iteration) {
        uint8_t candidate[sizeof golden];
        uint8_t original[sizeof golden];
        HelX3spView view;
        HelX3spView cleared;
        HelX3spError error;
        uint32_t value;
        size_t offset;
        memcpy(candidate, golden, sizeof candidate);
        value = next_corpus_value(&seed);
        offset = (size_t)(value % (uint32_t)sizeof candidate);
        candidate[offset] ^= (uint8_t)(1u << (next_corpus_value(&seed) & 7u));
        memcpy(original, candidate, sizeof original);
        memset(&view, 0xA5, sizeof view);
        memset(&cleared, 0, sizeof cleared);
        assert(hel_x3sp_parse(candidate, sizeof candidate, &view, &error) != HEL_X3SP_ERROR_OK);
        assert(memcmp(&view, &cleared, sizeof view) == 0);
        assert(memcmp(candidate, original, sizeof candidate) == 0);
    }
}

static void test_null_arguments_clear_outputs(void)
{
    HelX3spView view;
    HelX3spView cleared;
    HelX3spSpan span;
    uint32_t value = UINT32_C(0xffffffff);

    memset(&view, 0xA5, sizeof view);
    memset(&cleared, 0, sizeof cleared);
    assert(hel_x3sp_parse(NULL, sizeof golden, &view, NULL) == HEL_X3SP_ERROR_ARGUMENT);
    assert(memcmp(&view, &cleared, sizeof view) == 0);
    assert(hel_x3sp_parse(golden, sizeof golden, NULL, NULL) == HEL_X3SP_ERROR_ARGUMENT);
    memset(&span, 0xA5, sizeof span);
    assert(hel_x3sp_record(NULL, 1, 0, &span) != 0);
    assert(span.data == NULL && span.size == 0);
    assert(hel_x3sp_span_u32((HelX3spSpan){NULL, 4}, 0, &value) != 0);
    assert(value == 0);
}

static unsigned split_manifest_fields(char *line, char *fields[], unsigned capacity)
{
    unsigned count = 0u;
    char *token = strtok(line, "\t\r\n");
    while (token != NULL) {
        assert(count < capacity);
        fields[count++] = token;
        token = strtok(NULL, "\t\r\n");
    }
    return count;
}

static uint32_t manifest_u32(const char *text)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > UINT32_MAX)
        fprintf(stderr, "invalid unsigned manifest field: '%s'\n", text);
    assert(end != text && *end == '\0' && value <= UINT32_MAX);
    return (uint32_t)value;
}

static int32_t manifest_i32(const char *text)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    assert(end != text && *end == '\0' && value >= INT32_MIN && value <= INT32_MAX);
    return (int32_t)value;
}

static void assert_manifest_string(const HelX3spView *view, uint32_t offset,
                                   uint32_t length, const char *expected)
{
    HelX3spSpan value;
    size_t expected_length = strlen(expected);
    assert(expected_length == length);
    assert(hel_x3sp_string(view, offset, length, &value) == HEL_X3SP_ERROR_OK);
    assert(value.size == expected_length);
    assert(memcmp(value.data, expected, expected_length) == 0);
}

static uint32_t span_u32_at(HelX3spSpan record, size_t offset)
{
    uint32_t value = 0u;
    assert(hel_x3sp_span_u32(record, offset, &value) == HEL_X3SP_ERROR_OK);
    return value;
}

static void assert_digest_hex(const uint8_t *digest, const char *expected)
{
    static const char digits[] = "0123456789abcdef";
    size_t index;
    assert(strlen(expected) == 64u);
    for (index = 0u; index < 32u; ++index) {
        assert(digits[digest[index] >> 4] == expected[index * 2u]);
        assert(digits[digest[index] & 15u] == expected[index * 2u + 1u]);
    }
}

static void assert_manifest_row(const HelX3spView *view, const char *line,
                                uint32_t interface_indices[2][2],
                                uint32_t resource_indices[2],
                                uint32_t declaration_indices[2],
                                uint32_t access_indices[2],
                                uint32_t word_indices[2],
                                uint32_t *row_counts)
{
    char copy[4096];
    char *field[16];
    unsigned count;
    uint32_t stage_index, local_index;
    HelX3spSpan record;
    assert(strlen(line) < sizeof copy);
    strcpy(copy, line);
    count = split_manifest_fields(copy, field, 16u);
    assert(count > 0u);

    if (strcmp(field[0], "PAIR") == 0) {
        HelX3spSpan pair;
        assert(count == 4u);
        assert(hel_x3sp_record(view, 1u, 0u, &pair) == HEL_X3SP_ERROR_OK);
        assert_manifest_string(view, span_u32_at(pair, 0u), span_u32_at(pair, 4u), field[1]);
        assert_manifest_string(view, span_u32_at(pair, 8u), span_u32_at(pair, 12u), field[2]);
        assert_digest_hex(pair.data + 16u, field[3]);
        row_counts[0]++;
    } else if (strcmp(field[0], "COST") == 0) {
        assert(count == 7u);
        assert(manifest_u32(field[1]) == view->program_control);
        assert(manifest_u32(field[2]) == view->interpolator_count);
        assert(manifest_u32(field[3]) == view->program_upload_dwords);
        assert(manifest_u32(field[4]) == view->constant_upload_dwords);
        assert(manifest_u32(field[5]) == view->state_upload_dwords);
        assert(manifest_u32(field[6]) == view->pair_upload_dwords);
        row_counts[1]++;
    } else if (strcmp(field[0], "STAGE") == 0) {
        assert(count == 13u);
        stage_index = manifest_u32(field[1]);
        assert(stage_index < 2u);
        assert(hel_x3sp_record(view, 2u, stage_index, &record) == HEL_X3SP_ERROR_OK);
        assert(manifest_u32(field[2]) == span_u32_at(record, 0u));
        assert(manifest_u32(field[3]) == span_u32_at(record, 4u));
        assert(manifest_u32(field[4]) == span_u32_at(record, 8u));
        assert(manifest_u32(field[5]) == span_u32_at(record, 16u));
        assert(manifest_u32(field[6]) == span_u32_at(record, 44u));
        assert(manifest_u32(field[7]) == span_u32_at(record, 48u));
        assert(manifest_i32(field[8]) == (span_u32_at(record, 52u) == UINT32_MAX
            ? -1 : (int32_t)span_u32_at(record, 52u)));
        assert_manifest_string(view, span_u32_at(record, 72u), span_u32_at(record, 76u), field[9]);
        assert_manifest_string(view, span_u32_at(record, 80u), span_u32_at(record, 84u), field[10]);
        assert_digest_hex(record.data + 88u, field[11]);
        assert_digest_hex(record.data + 120u, field[12]);
        row_counts[2]++;
    } else if (strcmp(field[0], "WORD") == 0) {
        uint32_t word = 0u;
        char *end = NULL;
        unsigned long expected;
        assert(count == 4u);
        stage_index = manifest_u32(field[1]);
        assert(stage_index < 2u);
        local_index = manifest_u32(field[2]);
        assert(local_index == word_indices[stage_index]++);
        expected = strtoul(field[3], &end, 16);
        assert(end != field[3] && *end == '\0' && expected <= UINT32_MAX);
        assert(hel_x3sp_word(view, stage_index, local_index, &word) == HEL_X3SP_ERROR_OK);
        assert(word == (uint32_t)expected);
        row_counts[3]++;
    } else if (strcmp(field[0], "INTERFACE") == 0) {
        uint32_t direction;
        uint32_t range_offset, range_count, local_index;
        HelX3spSpan stage;
        assert(count == 11u);
        stage_index = manifest_u32(field[1]);
        assert(stage_index < 2u);
        direction = strcmp(field[2], "I") == 0 ? 0u : 1u;
        assert(direction < 2u);
        assert(hel_x3sp_record(view, 2u, stage_index, &stage) == HEL_X3SP_ERROR_OK);
        range_offset = span_u32_at(stage, direction == 0u ? 20u : 28u);
        range_count = span_u32_at(stage, direction == 0u ? 24u : 32u);
        local_index = interface_indices[stage_index][direction]++;
        assert(local_index < range_count);
        assert(hel_x3sp_record(view, 4u,
            (range_offset - view->sections[3].offset) / 36u + local_index,
            &record) == HEL_X3SP_ERROR_OK);
        assert(manifest_u32(field[3]) == span_u32_at(record, 0u));
        assert(manifest_u32(field[4]) == span_u32_at(record, 4u));
        assert(manifest_u32(field[5]) == span_u32_at(record, 8u));
        assert(manifest_i32(field[6]) == (span_u32_at(record, 12u) == UINT32_MAX ? -1 : (int32_t)span_u32_at(record, 12u)));
        assert(manifest_i32(field[7]) == (span_u32_at(record, 16u) == UINT32_MAX ? -1 : (int32_t)span_u32_at(record, 16u)));
        assert(manifest_u32(field[8]) == span_u32_at(record, 20u));
        assert(manifest_u32(field[9]) == span_u32_at(record, 24u));
        assert(manifest_u32(field[10]) == span_u32_at(record, 28u));
        row_counts[4]++;
    } else if (strcmp(field[0], "RESOURCE") == 0) {
        assert(count == 11u);
        stage_index = manifest_u32(field[1]);
        assert(stage_index < 2u);
        local_index = manifest_u32(field[2]);
        assert(local_index == resource_indices[stage_index]++);
        assert(hel_x3sp_record(view, 5u, row_counts[5]++, &record) == HEL_X3SP_ERROR_OK);
        assert(manifest_u32(field[3]) == span_u32_at(record, 0u));
        assert_manifest_string(view, span_u32_at(record, 4u), span_u32_at(record, 8u), field[4]);
        assert(manifest_u32(field[5]) == span_u32_at(record, 12u));
        assert(manifest_u32(field[6]) == span_u32_at(record, 16u));
        assert(manifest_u32(field[7]) == span_u32_at(record, 20u));
        assert(manifest_u32(field[8]) == span_u32_at(record, 24u));
        assert(manifest_i32(field[9]) == (span_u32_at(record, 28u) == UINT32_MAX ? -1 : (int32_t)span_u32_at(record, 28u)));
        assert(manifest_i32(field[10]) == (span_u32_at(record, 32u) == UINT32_MAX ? -1 : (int32_t)span_u32_at(record, 32u)));
    } else if (strcmp(field[0], "DECL") == 0) {
        assert(count == 6u);
        stage_index = manifest_u32(field[1]);
        assert(stage_index < 2u);
        local_index = declaration_indices[stage_index]++;
        assert(hel_x3sp_record(view, 6u, row_counts[6]++, &record) == HEL_X3SP_ERROR_OK);
        assert(manifest_u32(field[2]) == span_u32_at(record, 8u));
        assert_manifest_string(view, span_u32_at(record, 0u), span_u32_at(record, 4u), field[3]);
        assert(manifest_u32(field[4]) == span_u32_at(record, 12u));
        assert(manifest_u32(field[5]) == span_u32_at(record, 16u));
        (void)local_index;
    } else if (strcmp(field[0], "USED") == 0) {
        uint32_t absolute_declaration, declaration_count, declaration_offset, i;
        int found = 0;
        HelX3spSpan stage;
        assert(count == 8u);
        stage_index = manifest_u32(field[1]);
        assert(stage_index < 2u);
        local_index = access_indices[stage_index]++;
        assert(hel_x3sp_record(view, 7u, row_counts[7]++, &record) == HEL_X3SP_ERROR_OK);
        assert(hel_x3sp_record(view, 2u, stage_index, &stage) == HEL_X3SP_ERROR_OK);
        declaration_offset = span_u32_at(stage, 56u);
        declaration_count = span_u32_at(stage, 60u);
        for (i = 0u; i < declaration_count; ++i) {
            HelX3spSpan declaration;
            assert(hel_x3sp_record(view, 6u,
                (declaration_offset - view->sections[5].offset) / 24u + i,
                &declaration) == HEL_X3SP_ERROR_OK);
            if (span_u32_at(declaration, 12u) == manifest_u32(field[3])) {
                HelX3spSpan name;
                assert(hel_x3sp_string(view, span_u32_at(declaration, 0u), span_u32_at(declaration, 4u), &name) == HEL_X3SP_ERROR_OK);
                if (name.size == strlen(field[2]) && memcmp(name.data, field[2], name.size) == 0) {
                    absolute_declaration = declaration_offset + i * 24u;
                    found = 1;
                    break;
                }
            }
        }
        assert(found);
        assert(span_u32_at(record, 0u) == absolute_declaration);
        assert(manifest_u32(field[3]) < span_u32_at(stage, 44u));
        assert(manifest_u32(field[4]) == span_u32_at(record, 4u));
        assert(manifest_u32(field[5]) == span_u32_at(record, 8u));
        assert(manifest_u32(field[6]) == span_u32_at(record, 12u));
        assert(manifest_i32(field[7]) == (span_u32_at(record, 16u) == 0u
            ? -1 : (int32_t)span_u32_at(record, 16u)));
        (void)local_index;
    } else {
        fprintf(stderr, "unknown exporter manifest row: %s\n", field[0]);
        assert(0);
    }
}

static void test_real_export_artifacts(void)
{
    static const char *const allowed_files[] = {
        "mesh.x3sp", "batch-unoptimized.x3sp", "batch-optimized.x3sp"
    };
    FILE *manifest = fopen("/out/integration/expected.txt", "rb");
    char line[4096];
    int pending_file = 0;
    uint32_t files_seen = 0u;
    assert(manifest != NULL);
    for (;;) {
        char *fields[4];
        unsigned count;
        uint32_t file_index;
        FILE *artifact_file;
        uint8_t bytes[1048576];
        size_t size;
        HelX3spView view;
        HelX3spError error;
        int parse_status;
        uint32_t interface_indices[2][2] = {{0u, 0u}, {0u, 0u}};
        uint32_t resource_indices[2] = {0u, 0u};
        uint32_t declaration_indices[2] = {0u, 0u};
        uint32_t access_indices[2] = {0u, 0u};
        uint32_t word_indices[2] = {0u, 0u};
        uint32_t row_counts[8] = {0u};
        uint32_t i;

        if (!pending_file && fgets(line, sizeof line, manifest) == NULL) break;
        pending_file = 0;
        assert(strlen(line) < sizeof bytes);
        count = split_manifest_fields(line, fields, 4u);
        assert(count == 2u && strcmp(fields[0], "FILE") == 0);
        for (file_index = 0u; file_index < 3u; ++file_index)
            if (strcmp(fields[1], allowed_files[file_index]) == 0) break;
        assert(file_index < 3u);
        assert((files_seen & (1u << file_index)) == 0u);
        files_seen |= 1u << file_index;
        {
            char path[128];
            int path_size = snprintf(path, sizeof path, "/out/integration/%s", fields[1]);
            assert(path_size > 0 && (size_t)path_size < sizeof path);
            artifact_file = fopen(path, "rb");
        }
        assert(artifact_file != NULL);
        size = fread(bytes, 1u, sizeof bytes, artifact_file);
        assert(!ferror(artifact_file));
        assert(feof(artifact_file));
        assert(fclose(artifact_file) == 0);
        parse_status = hel_x3sp_parse(bytes, size, &view, &error);
        if (parse_status != HEL_X3SP_ERROR_OK)
            fprintf(stderr, "real artifact %s parse failed: code=%u offset=%u size=%zu\n",
                    allowed_files[file_index], error.code, error.offset, size);
        assert(parse_status == HEL_X3SP_ERROR_OK);

        while (fgets(line, sizeof line, manifest) != NULL) {
            if (strncmp(line, "FILE\t", 5u) == 0) {
                pending_file = 1;
                break;
            }
            assert_manifest_row(&view, line, interface_indices, resource_indices,
                declaration_indices, access_indices, word_indices, row_counts);
        }
        assert(row_counts[0] == 1u && row_counts[1] == 1u && row_counts[2] == 2u);
        assert(row_counts[3] == view.sections[2].count);
        assert(row_counts[4] == view.sections[3].count);
        assert(row_counts[5] == view.sections[4].count);
        assert(row_counts[6] == view.sections[5].count);
        assert(row_counts[7] == view.sections[6].count);
        for (i = 0u; i < 2u; ++i) {
            HelX3spSpan stage;
            assert(hel_x3sp_record(&view, 2u, i, &stage) == HEL_X3SP_ERROR_OK);
            assert(word_indices[i] == span_u32_at(stage, 16u));
            assert(resource_indices[i] == span_u32_at(stage, 40u));
            assert(declaration_indices[i] == span_u32_at(stage, 60u));
            assert(access_indices[i] == span_u32_at(stage, 68u));
        }
        for (i = 0u; i < 2u; ++i) {
            HelX3spSpan stage;
            assert(hel_x3sp_record(&view, 2u, i, &stage) == HEL_X3SP_ERROR_OK);
            assert(interface_indices[i][0] == span_u32_at(stage, 24u));
            assert(interface_indices[i][1] == span_u32_at(stage, 32u));
        }
    }
    assert(files_seen == 7u);
    assert(fclose(manifest) == 0);
    puts("real exporter artifacts match compiler-snapshot manifests");
}

int main(void)
{
    read_fixture();
    test_golden_parse_and_borrowed_accessors();
    test_rich_fixture_baseline();
    test_sparse_inputs_and_unused_matrix();
    test_repaired_integrity_semantic_mutations();
    test_semantic_reference_with_repaired_integrity();
    test_failure_clears_view_for_every_truncation();
    test_fixed_seed_bounded_mutation_corpus();
    test_null_arguments_clear_outputs();
    if (getenv("X3SP_INTEGRATION") != NULL)
        test_real_export_artifacts();
    puts("shader artifact semantic/accessor tests passed");
    return 0;
}
