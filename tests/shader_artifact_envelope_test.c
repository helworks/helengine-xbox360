#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/platform/xenia_guest/x360_shader_artifact_internal.h"
#include "../src/platform/xenia_guest/x360_shader_artifact_sha256.h"

static void assert_digest(const uint8_t actual[32], const char *hex)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < 32; ++i) {
        assert(digits[actual[i] >> 4] == hex[i * 2]);
        assert(digits[actual[i] & 15] == hex[i * 2 + 1]);
    }
    assert(hex[64] == '\0');
}

static void hash_once(const uint8_t *data, size_t size, uint8_t digest[32])
{
    HelX3spSha256 ctx;
    hel_x3sp_sha256_init(&ctx);
    hel_x3sp_sha256_update(&ctx, data, size);
    hel_x3sp_sha256_final(&ctx, digest);
}

static void test_standard_vectors(void)
{
    static const uint8_t abc[] = {'a', 'b', 'c'};
    uint8_t digest[32];

    hash_once(NULL, 0, digest);
    assert_digest(digest, "e3b0c44298fc1c149afbf4c8996fb924"
                          "27ae41e4649b934ca495991b7852b855");
    hash_once(abc, sizeof abc, digest);
    assert_digest(digest, "ba7816bf8f01cfea414140de5dae2223"
                          "b00361a396177a9cb410ff61f20015ad");
}

static void test_padding_and_split_boundaries(void)
{
    static const size_t sizes[] = {55, 56, 63, 64, 65, 130};
    static const char *const expected[] = {
        "2900465fcb533e05a158fd2b3be0e5e3b03740d83060aa3580e0d98a96bf2384",
        "31454ff48ef36af2f08fd511bdc37d9d5855ac23e992e5ff5445cb6b7674a674",
        "5f6401b96532c36de4e65beec0409b69b1d181864c8009b7a04f43e5d56350d1",
        "94eb5de4943613fd048dc93393ab06877405faa39c11f53e9386083339833e7e",
        "fc518669b6eb4b4dd91827ecacef86689c725bd5bab888fd3b26dbb196eec954",
        "d35b74124cb85cfac62bfeaf3a9771efab3e0ef88fc43702c35a6efe09467cc1"
    };
    uint8_t data[130];
    size_t i;

    for (i = 0; i < sizeof data; ++i)
        data[i] = (uint8_t)(i * 37u + 11u);

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        size_t size = sizes[i];
        uint8_t before[130];
        uint8_t digest[32];
        HelX3spSha256 ctx;

        memcpy(before, data, size);
        hel_x3sp_sha256_init(&ctx);
        hel_x3sp_sha256_update(&ctx, data, size / 2);
        hel_x3sp_sha256_update(&ctx, data + size / 2, size - size / 2);
        hel_x3sp_sha256_final(&ctx, digest);
        assert_digest(digest, expected[i]);
        assert(memcmp(data, before, size) == 0);

        hel_x3sp_sha256_init(&ctx);
        for (size_t offset = 0; offset < size;) {
            size_t chunk = size - offset;
            if (chunk > 7)
                chunk = 7;
            hel_x3sp_sha256_update(&ctx, data + offset, chunk);
            offset += chunk;
        }
        hel_x3sp_sha256_final(&ctx, digest);
        assert_digest(digest, expected[i]);
        assert(memcmp(data, before, size) == 0);
    }
}

static uint8_t golden[668];
static uint8_t oversized_file[1048577];

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

static void expect_failure(const uint8_t *data, size_t size,
                           uint32_t expected_code, uint32_t expected_offset)
{
    HelX3spEnvelope view;
    HelX3spEnvelope zero_view;
    HelX3spError error = {0, 0};
    int result;

    memset(&view, 0xA5, sizeof view);
    memset(&zero_view, 0, sizeof zero_view);
    result = hel_x3sp_validate_envelope(data, size, &view, &error);
    assert(result == (int)expected_code);
    assert(error.code == expected_code);
    if (expected_offset != UINT32_MAX)
        assert(error.offset == expected_offset);
    assert(memcmp(&view, &zero_view, sizeof view) == 0);
}

static void test_fixture_envelope_and_truncations(void)
{
    uint8_t unaligned[sizeof golden + 1];
    uint8_t unchanged[sizeof golden + 1];
    HelX3spEnvelope view;
    HelX3spError error;
    size_t size;

    unaligned[0] = 0x5a;
    memcpy(unaligned + 1, golden, sizeof golden);
    memcpy(unchanged, unaligned, sizeof unaligned);
    assert(hel_x3sp_validate_envelope(unaligned + 1, sizeof golden, &view, &error) == 0);
    assert(error.code == 0);
    assert(view.data == unaligned + 1);
    assert(view.size == sizeof golden);
    assert(view.sections[2].offset == 560);
    assert(view.sections[2].length == 24);
    assert(view.sections[2].count == 6);
    assert(hel_x3sp_read_le32(golden + 560) == UINT32_C(0x12345678));
    assert(memcmp(unaligned, unchanged, sizeof unaligned) == 0);

    {
        uint8_t whole_file_digest[32];
        hash_once(golden, sizeof golden, whole_file_digest);
        assert_digest(whole_file_digest,
                      "698aba97a517880a854b933784738432"
                      "85f108851e72c1a3d1ba5d3ac09071c6");
    }

    for (size = 0; size < sizeof golden; ++size)
        expect_failure(golden, size, HEL_X3SP_ERROR_ENVELOPE, UINT32_MAX);
}

static void test_null_arguments_and_integrity_failure(void)
{
    HelX3spEnvelope view;
    HelX3spEnvelope zero_view;
    HelX3spError error;
    uint8_t mutated[sizeof golden];

    memset(&view, 0xA5, sizeof view);
    memset(&zero_view, 0, sizeof zero_view);
    assert(hel_x3sp_validate_envelope(NULL, sizeof golden, &view, &error) == HEL_X3SP_ERROR_ARGUMENT);
    assert(memcmp(&view, &zero_view, sizeof view) == 0);
    assert(error.code == HEL_X3SP_ERROR_ARGUMENT);
    assert(error.offset == 0);
    assert(hel_x3sp_validate_envelope(golden, sizeof golden, NULL, &error) == HEL_X3SP_ERROR_ARGUMENT);
    assert(error.code == HEL_X3SP_ERROR_ARGUMENT);
    assert(hel_x3sp_validate_envelope(golden, sizeof golden, &view, NULL) == 0);
    expect_failure(oversized_file, sizeof oversized_file,
                   HEL_X3SP_ERROR_ENVELOPE, 0);

    memcpy(mutated, golden, sizeof mutated);
    mutated[560] ^= 0x80;
    expect_failure(mutated, sizeof mutated, HEL_X3SP_ERROR_INTEGRITY, 32);
}

static void test_repaired_header_and_directory_errors(void)
{
    uint8_t mutated[sizeof golden];

#define REPAIR_AND_EXPECT(offset_, value_, code_, error_offset_) \
    do { \
        memcpy(mutated, golden, sizeof mutated); \
        write_le32(mutated + (offset_), (value_)); \
        repair_artifact_integrity(mutated, sizeof mutated); \
        expect_failure(mutated, sizeof mutated, (code_), (error_offset_)); \
    } while (0)

    REPAIR_AND_EXPECT(12, 664, HEL_X3SP_ERROR_ENVELOPE, 12);
    REPAIR_AND_EXPECT(12, 1048577, HEL_X3SP_ERROR_ENVELOPE, 12);
    REPAIR_AND_EXPECT(4, 2, HEL_X3SP_ERROR_UNSUPPORTED, 4);
    REPAIR_AND_EXPECT(16, 7, HEL_X3SP_ERROR_UNSUPPORTED, 16);
    REPAIR_AND_EXPECT(28, 1, HEL_X3SP_ERROR_UNSUPPORTED, 28);
    REPAIR_AND_EXPECT(64, 2, HEL_X3SP_ERROR_ENVELOPE, 64);
    REPAIR_AND_EXPECT(64, 9, HEL_X3SP_ERROR_UNSUPPORTED, 64);
    REPAIR_AND_EXPECT(64 + 3 * 16 + 4, 576, HEL_X3SP_ERROR_ENVELOPE, 116);
    REPAIR_AND_EXPECT(64 + 3 * 16 + 4, UINT32_MAX, HEL_X3SP_ERROR_RANGE, 116);
    REPAIR_AND_EXPECT(64 + 2 * 16 + 8, UINT32_MAX, HEL_X3SP_ERROR_RANGE, 104);
    REPAIR_AND_EXPECT(64 + 2 * 16 + 12, UINT32_MAX, HEL_X3SP_ERROR_RANGE, 108);

#undef REPAIR_AND_EXPECT

    memcpy(mutated, golden, sizeof mutated);
    mutated[667] = 1;
    repair_artifact_integrity(mutated, sizeof mutated);
    expect_failure(mutated, sizeof mutated, HEL_X3SP_ERROR_ENVELOPE, 667);
}

static void test_stage_word_range_and_digest(void)
{
    uint8_t mutated[sizeof golden];

    memcpy(mutated, golden, sizeof mutated);
    write_le32(mutated + 240 + 12, UINT32_C(0xfffffffc));
    repair_artifact_integrity(mutated, sizeof mutated);
    expect_failure(mutated, sizeof mutated, HEL_X3SP_ERROR_RANGE, 252);

    memcpy(mutated, golden, sizeof mutated);
    mutated[560] ^= 1;
    repair_artifact_integrity(mutated, sizeof mutated);
    expect_failure(mutated, sizeof mutated, HEL_X3SP_ERROR_INTEGRITY, 240 + 120);

    /* The second stage range must be checked before hashing the first digest. */
    memcpy(mutated, golden, sizeof mutated);
    mutated[240 + 120] ^= 1;
    write_le32(mutated + 400 + 12, UINT32_C(0xfffffffc));
    repair_artifact_integrity(mutated, sizeof mutated);
    expect_failure(mutated, sizeof mutated, HEL_X3SP_ERROR_RANGE, 400 + 12);
}

int main(void)
{
    read_fixture();
    test_standard_vectors();
    test_padding_and_split_boundaries();
    test_fixture_envelope_and_truncations();
    test_null_arguments_and_integrity_failure();
    test_repaired_header_and_directory_errors();
    test_stage_word_range_and_digest();
    puts("shader artifact SHA/envelope tests passed");
    return 0;
}
