#include "xenia_guest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t captured[4096], captured_count;
static void capture(void *context, uint32_t word) {
    (void)context;
    assert(captured_count < 4096);
    captured[captured_count++] = word;
}

static void test_mesh_material(void) {
    char path[78];
    HelShaderProgram program;
    assert(hel_shader_mesh_path("mesh-material", "mesh.vs", "mesh.ps", path) == 0);
    /* Independently hashed BinaryWriter string tuple, not the cooker's path helper. */
    assert(strcmp(path, "shaders/0eae7898bce1ff9da602d567f4afd023ab14769b1f192ca410c053c3e5eceeee.x3sp") == 0);
    assert(hel_shader_program_open(path, "mesh-material", "mesh.vs", "mesh.ps",
        "MeshMaterialV1", 1, &program) == 0);
    assert(hel_shader_mesh_validate(&program) == 0);
    assert(program.constant_words[0] == 16 && program.constant_words[1] == 4);
    assert(program.banks[0] == 0 && program.banks[1] == 256);
    const float matrix[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    /* Default HLSL column-major becomes SPIR-V RowMajor on the transposed
       logical matrix: uploaded vec4s must contain Engine matrix columns. */
    const float expected[16] = {1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16};
    uint32_t vs[16], ps[4] = {0x3f000000, 0x3e800000, 0x3e000000, 0x3f800000};
    assert(hel_shader_mesh_constants(&program, matrix, vs) == 0);
    assert(memcmp(vs, expected, sizeof(vs)) == 0);
    uint32_t cost = program.view.pair_upload_dwords;
    assert(hel_shader_program_emit(&program, vs, 16, ps, 3, cost, capture, 0) != 0);
    assert(captured_count == 0);
    assert(hel_shader_program_emit(&program, vs, 16, ps, 4, cost - 1, capture, 0) != 0);
    assert(captured_count == 0);
    assert(hel_shader_program_emit(&program, vs, 16, ps, 4, cost, capture, 0) == 0);
    assert(captured_count == cost);
    /* Final packets: 18 words for VS bank0 followed by 6 for PS bank256. */
    assert(captured[cost - 23] == 0);
    assert(memcmp(captured + cost - 22, vs, sizeof(vs)) == 0);
    assert(captured[cost - 5] == 256);
    assert(memcmp(captured + cost - 4, ps, sizeof(ps)) == 0);
    assert(hel_shader_program_open(path, "wrong", "mesh.vs", "mesh.ps",
        "MeshMaterialV1", 1, &program) != 0 && !program.view.data);
    assert(hel_shader_program_open(path, "mesh-material", "mesh.vs", "mesh.ps",
        "MeshMaterialV1", 2, &program) != 0 && !program.view.data);
}
void hel_log(const char *text) { (void)text; }
void hel_log_u32(const char *text, uint32_t value) { (void)text; (void)value; }
int main(void) {
    assert(hel_content_init() == HEL_OK);
    assert(hel_content_count() == 2);
    /* This fixture mirrors the real editor package, not the direct cooker root. */
    const uint8_t *bytes;
    uint32_t size;
    assert(hel_content_open("shaders/spritebatch-textured.x3sp", &bytes, &size) != HEL_OK);
    assert(hel_content_open("cooked/shaders/spritebatch-textured.x3sp", &bytes, &size) == HEL_OK);
    test_mesh_material();
    HelShaderProgram program;
    assert(hel_shader_program_open("shaders/spritebatch-textured.x3sp", "SpriteBatchShader",
        "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured", 2, &program) == 0);
    assert(program.profile == 2 && program.words[0] == 45 && program.words[1] == 15);
    assert(program.view.pair_upload_dwords == 94);
    assert(hel_shader_mesh_validate(&program) == HEL_X3SP_ERROR_UNSUPPORTED);
    assert(hel_shader_program_open("missing.x3sp", "SpriteBatchShader",
        "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured", 2, &program) != 0);
    assert(!program.view.data);
    assert(hel_shader_program_open("cooked/shaders/spritebatch-textured.x3sp", "SpriteBatchShader",
        "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured", 2, &program) == 0);
    char too_long[520];
    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    assert(hel_shader_program_open(too_long, "SpriteBatchShader",
        "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured", 2, &program) != 0);
    assert(!program.view.data);
    puts("Production cooker -> manifest staging -> HELC archive -> content lookup -> X3SP runtime passed");
}
