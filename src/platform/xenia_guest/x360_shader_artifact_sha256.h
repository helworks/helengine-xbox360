#ifndef HEL_X360_SHADER_ARTIFACT_SHA256_H
#define HEL_X360_SHADER_ARTIFACT_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* Private streaming state: fixed storage only, with no borrowed pointers. */
typedef struct HelX3spSha256 {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_size;
} HelX3spSha256;

void hel_x3sp_sha256_init(HelX3spSha256 *ctx);
void hel_x3sp_sha256_update(HelX3spSha256 *ctx, const uint8_t *data, size_t size);
void hel_x3sp_sha256_final(HelX3spSha256 *ctx, uint8_t digest[32]);

#endif
