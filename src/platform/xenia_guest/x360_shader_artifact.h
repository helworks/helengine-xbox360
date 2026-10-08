#ifndef HEL_X360_SHADER_ARTIFACT_H
#define HEL_X360_SHADER_ARTIFACT_H

#include <stddef.h>
#include <stdint.h>

typedef enum HelX3spErrorCode {
    HEL_X3SP_ERROR_OK = 0,
    HEL_X3SP_ERROR_ARGUMENT = 1,
    HEL_X3SP_ERROR_ENVELOPE = 2,
    HEL_X3SP_ERROR_INTEGRITY = 3,
    HEL_X3SP_ERROR_RANGE = 4,
    HEL_X3SP_ERROR_UNSUPPORTED = 5,
    HEL_X3SP_ERROR_SEMANTIC = 6
} HelX3spErrorCode;

typedef struct HelX3spSection {
    uint32_t offset;
    uint32_t length;
    uint32_t count;
} HelX3spSection;

typedef struct HelX3spError {
    uint32_t code;
    uint32_t offset;
} HelX3spError;

typedef struct HelX3spSpan {
    const uint8_t *data;
    size_t size;
} HelX3spSpan;

typedef struct HelX3spView {
    const uint8_t *data;
    size_t size;
    HelX3spSection sections[8];
    uint32_t program_control;
    uint32_t interpolator_count;
    uint32_t program_upload_dwords;
    uint32_t constant_upload_dwords;
    uint32_t state_upload_dwords;
    uint32_t pair_upload_dwords;
} HelX3spView;

/* Views borrow data. Keep it alive and unchanged until all uses are finished. */
int hel_x3sp_parse(const uint8_t *data, size_t size,
                   HelX3spView *out, HelX3spError *error);
int hel_x3sp_record(const HelX3spView *view, uint32_t kind, uint32_t index,
                    HelX3spSpan *out);
int hel_x3sp_string(const HelX3spView *view, uint32_t offset, uint32_t length,
                    HelX3spSpan *out);
int hel_x3sp_word(const HelX3spView *view, uint32_t stage_index,
                  uint32_t word_index, uint32_t *out);
int hel_x3sp_span_u32(HelX3spSpan span, size_t offset, uint32_t *out);

#endif
