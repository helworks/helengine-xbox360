/* Runs the actual texture backend with host allocation/fence stubs. No GPU
   commands or pixels from another process are read by this ownership test. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../src/platform/xenia_guest/xenia_guest.h"

static uint32_t allocations;
static uint32_t frees;
static uint32_t fetch_writes;

int hel_phys_alloc(const char *name, uint32_t size, HelPhysBlock *out) {
    (void)name;
    void *memory = mmap(0, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (memory == MAP_FAILED || (uintptr_t)memory > UINT32_MAX) return HEL_ERR_PLATFORM_ALLOC;
    out->virt = (uint32_t)(uintptr_t)memory;
    out->phys = out->virt;
    out->size = size;
    ++allocations;
    return HEL_OK;
}

void hel_phys_free(HelPhysBlock *block) {
    assert(block->virt != 0);
    assert(munmap((void *)(uintptr_t)block->virt, block->size) == 0);
    block->virt = block->phys = block->size = 0;
    ++frees;
}
void hel_log_u32(const char *label, uint32_t value) { (void)label; (void)value; }
void hel_log(const char *message) { (void)message; }
void hel_ring_put(uint32_t value) { (void)value; ++fetch_writes; }
void hel_texture_bind(HelTexture texture);

int main(void) {
    const uint8_t pixel[] = {12, 34, 56, 255};
    HelTexture first = 0, second = 0;
    assert(hel_texture_create(pixel, 1, 1, &first) == HEL_OK);
    assert(hel_texture_count() == 1 && allocations == 1 && frees == 0);
    hel_texture_bind(first);
    assert(fetch_writes == 7);
    hel_texture_release(first);
    hel_texture_release(first);
    hel_texture_release(0);
    hel_texture_release(UINT32_MAX);
    assert(hel_texture_count() == 0 && frees == 0); /* in-flight GPU remains safe */
    hel_texture_bind(first);
    assert(fetch_writes == 7); /* retired owner cannot record another draw */
    assert(hel_texture_create(pixel, 1, 1, &second) == HEL_OK);
    assert(first != second); /* pending memory cannot be reused before its fence */
    hel_texture_collect_retired();
    assert(frees == 1 && hel_texture_count() == 1);
    hel_texture_collect_retired();
    assert(frees == 1); /* repeat fence collection is idempotent */
    for (uint32_t cycle = 0; cycle < 96; ++cycle) {
        HelTexture texture = 0;
        assert(hel_texture_create(pixel, 1, 1, &texture) == HEL_OK);
        hel_texture_release(texture);
        hel_texture_collect_retired();
        assert(hel_texture_count() == 1);
    }
    hel_texture_release(second);
    hel_texture_collect_retired();
    hel_texture_release_all();
    hel_texture_release_all();
    assert(hel_texture_count() == 0 && allocations == frees);
    const HelVertex vertices[3] = {0};
    const uint16_t indices[3] = {0, 1, 2};
    HelMesh first_mesh = 0, second_mesh = 0;
    const uint32_t frees_before_meshes = frees;
    assert(hel_mesh_create(vertices, 3, indices, 3, &first_mesh) == HEL_OK);
    assert(hel_mesh_count() == 1);
    hel_mesh_release(first_mesh);
    hel_mesh_release(first_mesh);
    hel_mesh_release(0);
    hel_mesh_release(UINT32_MAX);
    assert(hel_mesh_count() == 0 && frees == frees_before_meshes);
    assert(hel_mesh_create(vertices, 3, indices, 3, &second_mesh) == HEL_OK);
    assert(first_mesh != second_mesh); /* earlier recorded draws still own their upload */
    hel_mesh_collect_retired();
    hel_mesh_collect_retired();
    assert(frees == frees_before_meshes + 1 && hel_mesh_count() == 1);
    for (uint32_t cycle = 0; cycle < 96; ++cycle) {
        HelMesh model = 0;
        assert(hel_mesh_create(vertices, 3, indices, 3, &model) == HEL_OK);
        hel_mesh_release(model);
        hel_mesh_collect_retired();
        assert(hel_mesh_count() == 1);
    }
    hel_mesh_release(second_mesh);
    hel_mesh_collect_retired();
    hel_mesh_release_all();
    hel_mesh_release_all();
    assert(hel_mesh_count() == 0 && allocations == frees);
    puts("texture/mesh ownership: pending fence, idempotent retirement/shutdown and 96 reuse cycles each PASS");
    return 0;
}
