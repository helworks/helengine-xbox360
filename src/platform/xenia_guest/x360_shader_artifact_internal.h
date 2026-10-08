#ifndef HEL_X360_SHADER_ARTIFACT_INTERNAL_H
#define HEL_X360_SHADER_ARTIFACT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "x360_shader_artifact.h"

typedef struct HelX3spEnvelope {
    const uint8_t *data;
    size_t size;
    HelX3spSection sections[8];
} HelX3spEnvelope;

int hel_x3sp_validate_envelope(const uint8_t *data, size_t size,
                               HelX3spEnvelope *out, HelX3spError *error);
int hel_x3sp_validate_semantics(HelX3spView *view, HelX3spError *error);
int hel_x3sp_validate_string_bytes(const uint8_t *data, size_t size);
/* Call only after proving that four bytes at p are within the input range. */
uint32_t hel_x3sp_read_le32(const uint8_t *p);

#endif
