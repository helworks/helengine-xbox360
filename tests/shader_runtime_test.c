#define _GNU_SOURCE
#include "x360_shader_runtime.h"
#include "xenia_guest.h"
#include "xenos_regs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t bytes[1048576];
static uint32_t byte_count;
static uint32_t emitted[4096], emitted_count;
static uint32_t available = 4096, resets;
static void *arena;
int hel_phys_alloc(const char *name, uint32_t size, HelPhysBlock *out) {
    (void)name;
    arena = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(arena != MAP_FAILED && (uintptr_t)arena <= UINT32_MAX);
    *out = (HelPhysBlock){(uint32_t)(uintptr_t)arena, 0x10000, size};
    return 0;
}
void hel_phys_free(HelPhysBlock *block) { (void)block; }
void hel_log(const char *message) { (void)message; }
void hel_log_u32(const char *message, uint32_t value) { (void)message; (void)value; }
uint32_t hel_video_width(void) { return 640; }
uint32_t hel_video_height(void) { return 480; }
uint32_t hel_ring_available(void) { return available; }
int hel_ring_overflowed(void) { return 0; }
int hel_shaders_validate(void) { return 0; }
void hel_shaders_emit_kind(HelShaderKind kind) { (void)kind; assert(0 && "batch must not upload legacy programs"); }
void hel_shaders_frame_reset(void) { ++resets; }
void hel_texture_bind(HelTexture texture) { assert(texture == 1); }
int hel_content_open(const char *path, const uint8_t **data, uint32_t *size) {
    *data = 0; *size = 0;
    if (strcmp(path, "batch.x3sp")) return HEL_ERR_MESH_INVALID;
    *data = bytes; *size = byte_count;
    return HEL_OK;
}
static void capture(void *context, uint32_t word) {
    assert(context == emitted);
    assert(emitted_count < sizeof(emitted) / sizeof(emitted[0]));
    emitted[emitted_count++] = word;
}
void hel_ring_put(uint32_t word) { capture(emitted, word); }
void hel_ring_reg(uint32_t index, uint32_t value) { hel_ring_put(XE_PACKET_TYPE0(index, 1)); hel_ring_put(value); }
void hel_ring_type3(uint32_t opcode, uint32_t count) { hel_ring_put(XE_PACKET_TYPE3(opcode, count)); }
static int contains(uint32_t first, uint32_t second) {
    for (uint32_t i = 0; i + 1 < emitted_count; ++i) if (emitted[i] == first && emitted[i + 1] == second) return 1;
    return 0;
}
static void read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    byte_count = (uint32_t)fread(bytes, 1, sizeof(bytes), file);
    assert(!ferror(file));
    assert(feof(file));
    fclose(file);
}
static int open_batch(HelShaderProgram *program) {
    return hel_shader_program_open("batch.x3sp", "task4/shared-batch2d-textured-optimized",
        "SpriteBatch.Textured.VS", "SpriteBatch.Textured.PS", "Textured", 2, program);
}
int main(void) {
    read_file("/out/integration/batch-optimized.x3sp");
    HelShaderProgram program;
    assert(open_batch(&program) == 0);
    assert(program.view.data == bytes);
    assert(program.words[0] == 45 && program.words[1] == 15);
    assert(program.constant_words[0] == 16 && program.constant_words[1] == 0);
    float matrix[16];
    uint32_t constants[16];
    for (uint32_t i = 0; i < 16; ++i) matrix[i] = (float)(i + 1);
    assert(hel_shader_program_projection(&program, matrix, constants) == 0);
    HelX3spSpan used;
    uint32_t major;
    assert(hel_x3sp_record(&program.view, 7, 0, &used) == 0);
    assert(hel_x3sp_span_u32(used, 12, &major) == 0);
    assert(major == 1); /* shaderc RowMajor represents the transposed HLSL matrix. */
    const float expected_projection[16] = {1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16};
    assert(memcmp(constants, expected_projection, sizeof(constants)) == 0);
    assert(hel_shader_program_emit(&program, constants, 15, 0, 0, 4096, capture, emitted) != 0);
    assert(emitted_count == 0);
    assert(hel_shader_program_emit(&program, constants, 16, 0, 0, 93, capture, emitted) != 0);
    assert(emitted_count == 0);
    assert(hel_shader_program_emit(&program, constants, 16, 0, 0, 94, capture, emitted) == 0);
    assert(emitted_count == 94);
    assert(emitted[0] == XE_PACKET_TYPE3(PM4_IM_LOAD_IMMEDIATE, 47));
    assert(emitted[1] == 0 && emitted[2] == 45);
    assert(emitted[48] == XE_PACKET_TYPE3(PM4_IM_LOAD_IMMEDIATE, 17));
    assert(emitted[49] == 1 && emitted[50] == 15);
    for (uint32_t stage = 0; stage < 2; ++stage) for (uint32_t i = 0; i < program.words[stage]; ++i) {
        uint32_t word;
        assert(hel_x3sp_word(&program.view, stage, i, &word) == 0);
        assert(emitted[(stage ? 51 : 3) + i] == word);
    }
    assert(emitted[76] == XE_PACKET_TYPE3(PM4_SET_CONSTANT, 17));
    assert(emitted[77] == 0);
    assert(memcmp(emitted + 78, constants, sizeof(constants)) == 0);
    assert(hel_ui_arena_init() == 0);
    float vertices[4 * 24] = {0};
    for (uint32_t i = 0; i < 96; ++i) vertices[i] = (float)i;
    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    HelBatchDraw batch = {&program, constants, {10, 20, 320, 240}, {12, 22, 100, 80}, 0};
    emitted_count = 0;
    available = 93;
    assert(hel_draw_batch(vertices, 4, indices, 6, 1, &batch) == HEL_ERR_RING_FULL);
    assert(emitted_count == 0);
    available = 4096;
    indices[5] = 4;
    assert(hel_draw_batch(vertices, 4, indices, 6, 1, &batch) == HEL_ERR_MESH_INVALID);
    assert(emitted_count == 0);
    indices[5] = 3;
    assert(hel_draw_batch(vertices, 4, indices, 6, 1, &batch) == 0);
    assert(resets == 1);
    assert(memcmp(arena, vertices, sizeof(vertices)) == 0);
    assert(memcmp((uint8_t *)arena + sizeof(vertices), indices, sizeof(indices)) == 0);
    assert(contains(XE_PACKET_TYPE0(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 1), 0x80000000u | 12u | (22u << 16)));
    assert(contains(XE_PACKET_TYPE0(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, 1), 112u | (102u << 16)));
    assert(contains(3u | 0x10000u, XE_ENDIAN_8IN32 | ((4u * 24u) << 2)));
    assert(contains(XE_PACKET_TYPE3(PM4_DRAW_INDX, 4), 0));
    assert(emitted_count < 94 + 128);
    assert(munmap(arena, 0x40000u) == 0);
    assert(hel_shader_program_init(bytes, byte_count, "wrong", "SpriteBatch.Textured.VS",
        "SpriteBatch.Textured.PS", "Textured", 2, &program) != 0);
    assert(program.view.data == 0);
    assert(hel_shader_program_init(bytes, byte_count, "task4/shared-batch2d-textured-optimized", "SpriteBatch.Textured.VS",
        "SpriteBatch.Textured.PS", "Textured", 1, &program) != 0);
    assert(program.view.data == 0);
    assert(open_batch(&program) == 0);
    bytes[byte_count - 1] ^= 1;
    assert(open_batch(&program) != 0 && program.view.data == 0);
    assert(hel_shader_program_open("absent", "a", "v", "p", "v", 2, &program) != 0 && program.view.data == 0);
    read_file("/out/integration/mesh.x3sp");
    assert(hel_shader_program_init(bytes, byte_count, "task4/legacy-mesh3d", "task4-mesh-vs",
        "task4-mesh-ps", "Task4LegacyMesh3D", 1, &program) == 0);
    uint32_t vs_bank[1024] = {0}, ps_bank[1024] = {0};
    emitted_count = 0;
    assert(hel_shader_program_emit(&program, vs_bank, program.constant_words[0], ps_bank,
        program.constant_words[1], 110, capture, emitted) == 0);
    assert(emitted_count == 110);
    /* Exercise the new production mesh draw, not just the generic uploader. */
    read_file("/out/staged/shaders/0eae7898bce1ff9da602d567f4afd023ab14769b1f192ca410c053c3e5eceeee.x3sp");
    assert(hel_shader_program_init(bytes, byte_count, "mesh-material", "mesh.vs",
        "mesh.ps", "MeshMaterialV1", 1, &program) == 0);
    HelVertex triangle[3] = {0};
    uint16_t mesh_indices[3] = {0, 1, 2};
    HelMesh mesh;
    assert(hel_mesh_create(triangle, 3, mesh_indices, 3, &mesh) == HEL_OK);
    const uint32_t color[4] = {0x3f000000, 0x3e800000, 0x3e000000, 0x3f800000};
    emitted_count = 0;
    assert(hel_draw_mesh_program(0, &program, matrix, color) == HEL_ERR_MESH_INVALID);
    assert(hel_draw_mesh_program(mesh, &program, 0, color) == HEL_ERR_MESH_INVALID);
    available = program.view.pair_upload_dwords + 127;
    assert(hel_draw_mesh_program(mesh, &program, matrix, color) == HEL_ERR_RING_FULL);
    assert(emitted_count == 0);
    available++;
    uint32_t old_resets = resets;
    assert(hel_draw_mesh_program(mesh, &program, matrix, color) == HEL_OK);
    assert(resets == old_resets + 1);
    assert(emitted_count > program.view.pair_upload_dwords && emitted_count <= available);
    assert(memcmp(emitted + program.view.pair_upload_dwords - 4, color, sizeof(color)) == 0);
    assert(contains(XE_PACKET_TYPE3(PM4_DRAW_INDX, 4), 0));
    assert(contains(3u | 0x10000u, XE_ENDIAN_8IN32 | ((3u * 12u) << 2)));
    hel_mesh_release_all();
    assert(munmap(arena, 0x10000u) == 0);
    puts("X3SP runtime identity, borrowed lifetime, projection, bounded upload and exact packet capture passed");
    return 0;
}
