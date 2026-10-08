#include "x360_shader_artifact_internal.h"

#include "x360_shader_artifact_sha256.h"

#include <string.h>

#define X3SP_HEADER_SIZE 64u
#define X3SP_DIRECTORY_OFFSET 64u
#define X3SP_DIRECTORY_RECORD_SIZE 16u
#define X3SP_SECTION_COUNT 8u
#define X3SP_FIRST_SECTION_OFFSET 192u
#define X3SP_MAX_FILE_SIZE 1048576u
#define X3SP_STAGE_RECORD_SIZE 160u

static const uint32_t section_record_sizes[X3SP_SECTION_COUNT] = {
    48u, 160u, 4u, 36u, 40u, 24u, 24u, 1u
};

static const uint32_t section_min_counts[X3SP_SECTION_COUNT] = {
    1u, 2u, 6u, 2u, 0u, 0u, 0u, 1u
};

static const uint32_t section_max_counts[X3SP_SECTION_COUNT] = {
    1u, 2u, 2048u, 24u, 4u, 512u, 512u, 262144u
};

uint32_t hel_x3sp_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int fail(HelX3spError *error, HelX3spErrorCode code, uint32_t offset)
{
    if (error != NULL) {
        error->code = (uint32_t)code;
        error->offset = offset;
    }
    return (int)code;
}

static size_t align4(size_t value)
{
    return (value + 3u) & ~(size_t)3u;
}

static int check_zero_padding(const uint8_t *data, size_t begin, size_t end,
                              HelX3spError *error)
{
    size_t i;
    for (i = begin; i < end; ++i) {
        if (data[i] != 0)
            return fail(error, HEL_X3SP_ERROR_ENVELOPE, (uint32_t)i);
    }
    return HEL_X3SP_ERROR_OK;
}

static void compute_artifact_digest(const uint8_t *data, size_t size,
                                    uint8_t digest[32])
{
    static const uint8_t virtual_zero_digest[32] = {0};
    HelX3spSha256 ctx;
    hel_x3sp_sha256_init(&ctx);
    hel_x3sp_sha256_update(&ctx, data, 32);
    hel_x3sp_sha256_update(&ctx, virtual_zero_digest, sizeof virtual_zero_digest);
    hel_x3sp_sha256_update(&ctx, data + X3SP_HEADER_SIZE, size - X3SP_HEADER_SIZE);
    hel_x3sp_sha256_final(&ctx, digest);
}

static int validate_and_hash_stage_code(const uint8_t *data,
                                        const HelX3spSection *stage_section,
                                        const HelX3spSection *microcode_section,
                                        HelX3spError *error)
{
    uint32_t offsets[2];
    uint32_t counts[2];
    size_t microcode_end = (size_t)microcode_section->offset + microcode_section->length;
    size_t i;

    /* Validate both ranges before hashing either stage. */
    for (i = 0; i < 2; ++i) {
        size_t record_offset = (size_t)stage_section->offset + i * X3SP_STAGE_RECORD_SIZE;
        uint32_t code_offset = hel_x3sp_read_le32(data + record_offset + 12);
        uint32_t word_count = hel_x3sp_read_le32(data + record_offset + 16);
        size_t code_offset_size = (size_t)code_offset;

        if (code_offset_size < microcode_section->offset ||
            code_offset_size > microcode_end ||
            (code_offset_size - microcode_section->offset) % 4u != 0) {
            return fail(error, HEL_X3SP_ERROR_RANGE, (uint32_t)(record_offset + 12));
        }
        if ((size_t)word_count > (microcode_end - code_offset_size) / 4u) {
            return fail(error, HEL_X3SP_ERROR_RANGE, (uint32_t)(record_offset + 16));
        }
        offsets[i] = code_offset;
        counts[i] = word_count;
    }

    for (i = 0; i < 2; ++i) {
        size_t record_offset = (size_t)stage_section->offset + i * X3SP_STAGE_RECORD_SIZE;
        HelX3spSha256 ctx;
        uint8_t digest[32];
        uint32_t word_index;

        hel_x3sp_sha256_init(&ctx);
        for (word_index = 0; word_index < counts[i]; ++word_index) {
            size_t byte_offset = (size_t)offsets[i] + (size_t)word_index * 4u;
            uint32_t word = hel_x3sp_read_le32(data + byte_offset);
            uint8_t big_endian_word[4];

            big_endian_word[0] = (uint8_t)(word >> 24);
            big_endian_word[1] = (uint8_t)(word >> 16);
            big_endian_word[2] = (uint8_t)(word >> 8);
            big_endian_word[3] = (uint8_t)word;
            hel_x3sp_sha256_update(&ctx, big_endian_word, sizeof big_endian_word);
        }
        hel_x3sp_sha256_final(&ctx, digest);
        if (memcmp(data + record_offset + 120, digest, sizeof digest) != 0)
            return fail(error, HEL_X3SP_ERROR_INTEGRITY, (uint32_t)(record_offset + 120));
    }

    return HEL_X3SP_ERROR_OK;
}

int hel_x3sp_validate_envelope(const uint8_t *data, size_t size,
                               HelX3spEnvelope *out, HelX3spError *error)
{
    HelX3spEnvelope result;
    uint8_t digest[32];
    uint32_t version, header_size, declared_size, target, section_count;
    uint32_t directory_offset, flags;
    size_t directory_end;
    size_t previous_end;
    size_t i;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (error != NULL) {
        error->code = HEL_X3SP_ERROR_OK;
        error->offset = 0;
    }
    if (out == NULL)
        return fail(error, HEL_X3SP_ERROR_ARGUMENT, 0);
    if (data == NULL)
        return fail(error, HEL_X3SP_ERROR_ARGUMENT, 0);
    if (size < X3SP_HEADER_SIZE || size > X3SP_MAX_FILE_SIZE)
        return fail(error, HEL_X3SP_ERROR_ENVELOPE, 0);
    if (memcmp(data, "X3SP", 4) != 0)
        return fail(error, HEL_X3SP_ERROR_ENVELOPE, 0);

    version = hel_x3sp_read_le32(data + 4);
    header_size = hel_x3sp_read_le32(data + 8);
    declared_size = hel_x3sp_read_le32(data + 12);
    target = hel_x3sp_read_le32(data + 16);
    section_count = hel_x3sp_read_le32(data + 20);
    directory_offset = hel_x3sp_read_le32(data + 24);
    flags = hel_x3sp_read_le32(data + 28);

    if (version != 1u)
        return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, 4);
    if (header_size != X3SP_HEADER_SIZE)
        return fail(error, HEL_X3SP_ERROR_ENVELOPE, 8);
    if (declared_size > X3SP_MAX_FILE_SIZE || (size_t)declared_size != size)
        return fail(error, HEL_X3SP_ERROR_ENVELOPE, 12);
    if (target != 8u)
        return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, 16);
    if (section_count != X3SP_SECTION_COUNT)
        return fail(error, HEL_X3SP_ERROR_ENVELOPE, 20);
    if (directory_offset != X3SP_DIRECTORY_OFFSET)
        return fail(error, HEL_X3SP_ERROR_ENVELOPE, 24);
    if (flags != 0u)
        return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, 28);

    if ((size_t)directory_offset > size ||
        (size_t)section_count > (size - (size_t)directory_offset) /
                                    X3SP_DIRECTORY_RECORD_SIZE) {
        return fail(error, HEL_X3SP_ERROR_RANGE, 24);
    }
    directory_end = (size_t)directory_offset +
                    (size_t)section_count * X3SP_DIRECTORY_RECORD_SIZE;

    compute_artifact_digest(data, size, digest);
    if (memcmp(data + 32, digest, sizeof digest) != 0)
        return fail(error, HEL_X3SP_ERROR_INTEGRITY, 32);

    memset(&result, 0, sizeof result);
    result.data = data;
    result.size = size;
    previous_end = directory_end;

    for (i = 0; i < X3SP_SECTION_COUNT; ++i) {
        size_t directory_record = (size_t)directory_offset + i * X3SP_DIRECTORY_RECORD_SIZE;
        uint32_t kind = hel_x3sp_read_le32(data + directory_record);
        uint32_t offset = hel_x3sp_read_le32(data + directory_record + 4);
        uint32_t length = hel_x3sp_read_le32(data + directory_record + 8);
        uint32_t count = hel_x3sp_read_le32(data + directory_record + 12);
        size_t expected_offset = align4(previous_end);
        size_t available;
        size_t expected_length;
        int padding_result;

        if (kind == 0u || kind > X3SP_SECTION_COUNT)
            return fail(error, HEL_X3SP_ERROR_UNSUPPORTED, (uint32_t)directory_record);
        if (kind != (uint32_t)i + 1u)
            return fail(error, HEL_X3SP_ERROR_ENVELOPE, (uint32_t)directory_record);
        if ((size_t)offset > size)
            return fail(error, HEL_X3SP_ERROR_RANGE, (uint32_t)(directory_record + 4));
        if ((size_t)offset != expected_offset)
            return fail(error, HEL_X3SP_ERROR_ENVELOPE, (uint32_t)(directory_record + 4));

        padding_result = check_zero_padding(data, previous_end, (size_t)offset, error);
        if (padding_result != HEL_X3SP_ERROR_OK)
            return padding_result;

        available = size - (size_t)offset;
        if ((size_t)count > available / section_record_sizes[i])
            return fail(error, HEL_X3SP_ERROR_RANGE, (uint32_t)(directory_record + 12));
        if (count < section_min_counts[i] || count > section_max_counts[i])
            return fail(error, HEL_X3SP_ERROR_RANGE, (uint32_t)(directory_record + 12));
        expected_length = (size_t)count * section_record_sizes[i];
        if ((size_t)length > available || (size_t)length != expected_length)
            return fail(error, HEL_X3SP_ERROR_RANGE, (uint32_t)(directory_record + 8));

        result.sections[i].offset = offset;
        result.sections[i].length = length;
        result.sections[i].count = count;
        previous_end = (size_t)offset + (size_t)length;
    }

    {
        size_t aligned_end = align4(previous_end);
        size_t padded_end = aligned_end < size ? aligned_end : size;
        int padding_result = check_zero_padding(data, previous_end, padded_end, error);
        if (padding_result != HEL_X3SP_ERROR_OK)
            return padding_result;
        if (aligned_end != size)
            return fail(error, HEL_X3SP_ERROR_ENVELOPE,
                        (uint32_t)(aligned_end < size ? aligned_end : size));
    }

    {
        int stage_result = validate_and_hash_stage_code(data, &result.sections[1],
                                                        &result.sections[2], error);
        if (stage_result != HEL_X3SP_ERROR_OK)
            return stage_result;
    }

    *out = result;
    if (error != NULL) {
        error->code = HEL_X3SP_ERROR_OK;
        error->offset = 0;
    }
    return HEL_X3SP_ERROR_OK;
}
