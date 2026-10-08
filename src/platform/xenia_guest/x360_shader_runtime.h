#ifndef HEL_X360_SHADER_RUNTIME_H
#define HEL_X360_SHADER_RUNTIME_H
#ifdef __cplusplus
extern "C" {
#endif
#include "x360_shader_artifact.h"

/* Borrows immutable content bytes for its entire lifetime. */
typedef struct {
    HelX3spView view;
    uint32_t words[2], constant_words[2], banks[2], profile;
} HelShaderProgram;

int hel_shader_program_init(const uint8_t *bytes, size_t size,
    const char *asset, const char *vertex, const char *pixel, const char *variant,
    uint32_t profile, HelShaderProgram *out);
int hel_shader_program_open(const char *path, const char *asset,
    const char *vertex, const char *pixel, const char *variant,
    uint32_t profile, HelShaderProgram *out);

/* Caller supplies complete logical constant banks; this API never invents values.
   Emit validates arguments and capacity before calling the sink even once. */
typedef void (*HelShaderWordSink)(void *context, uint32_t word);
int hel_shader_program_emit(const HelShaderProgram *program,
    const uint32_t *vs_constants, uint32_t vs_count,
    const uint32_t *ps_constants, uint32_t ps_count,
    uint32_t available, HelShaderWordSink sink, void *context);

/* Packs an Engine row-vector matrix for HLSL mul(vector, matrix), accounting
   for shaderc's transposed SPIR-V matrix and the used member's recorded major.
   Requires an exact single used matrix, preventing silently ignored uniforms. */
int hel_shader_program_projection(const HelShaderProgram *program,
    const float row_matrix[16], uint32_t constants[16]);

/* BinaryWriter UTF-8 string tuple identity, matching the managed cooker. */
int hel_shader_mesh_path(const char *asset, const char *vertex, const char *pixel, char path[78]);
/* MeshMaterialV1: Transform.mvp and Material.baseColor, no other resources. */
int hel_shader_mesh_validate(const HelShaderProgram *program);
int hel_shader_mesh_constants(const HelShaderProgram *program,
    const float row_matrix[16], uint32_t constants[16]);
#ifdef __cplusplus
}
#endif
#endif
