/* Exercises the real uploader with host physical-memory stubs, including copies, descriptors and deferred release. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../src/platform/xenia_guest/x360_native_texture.h"
#include "../src/platform/xenia_guest/xenia_guest.h"

static const uint32_t supported[] = {2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18,19,20,
    22,23,24,25,26,27,28,29,30,31,32,36,37,38,49,50,51,52,53,54,55,56,58,59,60,61};
static uint32_t allocations, frees, last_virt, ring[16], ring_count;
static int fail_allocation;

int hel_phys_alloc(const char *name, uint32_t size, HelPhysBlock *out) {
    (void)name;
    if (fail_allocation) return HEL_ERR_PLATFORM_ALLOC;
    void *memory = mmap(0,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    if (memory == MAP_FAILED || (uintptr_t)memory > UINT32_MAX) return HEL_ERR_PLATFORM_ALLOC;
    out->virt = (uint32_t)(uintptr_t)memory; out->phys = 0x01000000u + allocations*0x10000u; out->size = size;
    last_virt = out->virt; ++allocations;
    return HEL_OK;
}
void hel_phys_free(HelPhysBlock *block) {
    assert(block->virt && munmap((void *)(uintptr_t)block->virt,block->size) == 0);
    block->virt = block->phys = block->size = 0; ++frees;
}
void hel_log_u32(const char *label, uint32_t value) { (void)label; (void)value; }
void hel_log(const char *message) { (void)message; }
void hel_ring_put(uint32_t value) { assert(ring_count < 16); ring[ring_count++] = value; }

static void write_word(uint8_t *data, uint32_t offset, uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) data[offset+byte] = (uint8_t)(value >> (byte*8));
}
static uint8_t *payload_for(const HelX360NativeTextureLayout *layout) {
    uint8_t *bytes = calloc(1,56u + layout->byte_length); assert(bytes);
    const uint32_t words[] = {0x31543358,1,layout->format,0,layout->width,layout->height,
        layout->storage_width,layout->storage_height,layout->row_pitch_bytes,layout->byte_length,
        layout->swizzle,layout->signs,layout->num_format,(uint32_t)layout->exp_adjust};
    for (unsigned word = 0; word < 14; ++word) write_word(bytes,word*4,words[word]);
    for (uint32_t index = 0; index < layout->byte_length; ++index) bytes[56+index] = (uint8_t)(index*7+3);
    return bytes;
}
static int is_supported(uint32_t code) {
    for (unsigned index = 0; index < sizeof(supported)/sizeof(supported[0]); ++index)
        if (supported[index] == code) return 1;
    return 0;
}
static void verify_formats_and_payloads(void) {
    for (uint32_t code = 0; code < 64; ++code) {
        HelX360NativeTextureFormat format;
        assert(hel_x360_native_texture_format(code,&format) == is_supported(code));
    }
    HelX360NativeTextureFormat invalid;
    assert(!hel_x360_native_texture_format(64,&invalid));
    for (unsigned index = 0; index < sizeof(supported)/sizeof(supported[0]); ++index) {
        HelX360NativeTextureLayout layout, parsed, edge;
        const uint32_t code = supported[index];
        assert(hel_x360_native_texture_layout(code,3,5,&layout));
        HelX360NativeTextureFormat format;
        assert(hel_x360_native_texture_format(code,&format));
        assert(layout.storage_width % (32*format.block_width) == 0 && layout.row_pitch_bytes % 256 == 0);
        assert(layout.storage_height == ((5+format.block_height-1)/format.block_height)*format.block_height);
        if (code >= 18 && code <= 20) {
            assert(layout.storage_width == 128 && layout.storage_height == 8);
            assert(layout.row_pitch_bytes == (code == 18 ? 256u : 512u));
        }
        assert(hel_x360_native_texture_layout(code,8192,8192,&edge));
        assert(edge.storage_width == 8192);
        assert(!hel_x360_native_texture_layout(code,8193,1,&edge));
        assert(!hel_x360_native_texture_layout(code,1,0,&edge));
        uint8_t *bytes = payload_for(&layout);
        const uint8_t *texels = 0;
        assert(hel_x360_native_texture_parse(bytes,56+layout.byte_length,3,5,&parsed,&texels));
        assert(texels == bytes+56 && memcmp(&layout,&parsed,sizeof(layout)) == 0);
        assert(!hel_x360_native_texture_parse(bytes,55+layout.byte_length,3,5,&parsed,&texels));
        assert(!hel_x360_native_texture_parse(bytes,57+layout.byte_length,3,5,&parsed,&texels));
        assert(!hel_x360_native_texture_parse(bytes,55,3,5,&parsed,&texels));
        assert(!hel_x360_native_texture_parse(bytes,56+layout.byte_length,4,5,&parsed,&texels));
        for (uint32_t word = 0; word < 14; ++word) {
            bytes[word*4] ^= 0x80;
            assert(!hel_x360_native_texture_parse(bytes,56+layout.byte_length,3,5,&parsed,&texels));
            bytes[word*4] ^= 0x80;
        }
        uint32_t fetch[6];
        assert(hel_x360_native_texture_fetch(&layout,0x01000000,fetch));
        assert((fetch[1]&63u) == code && ((fetch[1]>>6)&3) == 0 && !(fetch[0]>>31));
        assert(((fetch[0]>>22)&511u)*32 == layout.storage_width);
        assert((fetch[2]&8191u)+1 == 3 && ((fetch[2]>>13)&8191u)+1 == 5);
        assert(((fetch[3]>>1)&4095u) == layout.swizzle && fetch[4] == 0 && fetch[5] == 512);
        assert(!hel_x360_native_texture_fetch(&layout,0x01000001,fetch));
        assert(!hel_x360_native_texture_fetch(&layout,0,fetch));
        assert(!hel_x360_native_texture_fetch(&layout,0x20000000,fetch));
        layout.swizzle ^= 1; assert(!hel_x360_native_texture_fetch(&layout,0x01000000,fetch)); layout.swizzle ^= 1;
        HelTexture texture = 0;
        assert(hel_texture_create_native(bytes,56+layout.byte_length,3,5,&texture) == HEL_OK);
        assert(memcmp((const void *)(uintptr_t)last_virt,bytes+56,layout.byte_length) == 0);
        ring_count = 0; hel_texture_bind(texture); assert(ring_count == 7 && (ring[2]&63u) == code);
        const uint32_t previous_frees = frees;
        hel_texture_release(texture); hel_texture_release(texture);
        assert(frees == previous_frees && hel_texture_count() == 0);
        hel_texture_bind(texture); assert(ring_count == 7);
        hel_texture_collect_retired(); assert(frees == previous_frees+1);
        hel_texture_collect_retired(); assert(frees == previous_frees+1);
        free(bytes);
    }
    HelX360NativeTextureLayout boundary;
    uint32_t fetch[6];
    assert(hel_x360_native_texture_layout(6,3,16,&boundary));
    assert(boundary.byte_length == 4096 && hel_x360_native_texture_fetch(&boundary,0x1FFFF000,fetch));
    assert(hel_x360_native_texture_layout(6,3,17,&boundary));
    assert(!hel_x360_native_texture_fetch(&boundary,0x1FFFF000,fetch));
    assert(hel_x360_native_texture_layout(38,8192,8192,&boundary));
    assert(!hel_x360_native_texture_fetch(&boundary,0x1000,fetch));
}
static void verify_lifetime_and_legacy(void) {
    const uint8_t glyph[] = {255,255,255,0,255,255,255,117,255,255,255,255};
    assert(hel_x360_preserve_anonymous_font_alpha(0,0,0));
    assert(!hel_x360_preserve_anonymous_font_alpha(0,0,1));
    assert(!hel_x360_preserve_anonymous_font_alpha(0,3,0));
    assert(!hel_x360_preserve_anonymous_font_alpha(14,0,0));
    HelTexture first = 0, second = 0;
    assert(hel_texture_create(glyph,3,1,&first) == HEL_OK);
    assert(memcmp((const void *)(uintptr_t)last_virt,glyph,sizeof(glyph)) == 0);
    ring_count = 0; hel_texture_bind(first); assert((ring[2]&63u) == 6 && ((ring[1]>>22)&511u) == 2);
    hel_texture_release(first);
    assert(hel_texture_create(glyph,3,1,&second) == HEL_OK && first != second);
    hel_texture_collect_retired(); assert(hel_texture_count() == 1);
    hel_texture_release(second); hel_texture_collect_retired();
    HelX360NativeTextureLayout layout;
    assert(hel_x360_native_texture_layout(18,3,5,&layout));
    uint8_t *bytes = payload_for(&layout);
    fail_allocation = 1;
    assert(hel_texture_create_native(bytes,56+layout.byte_length,3,5,&first) == HEL_ERR_PLATFORM_ALLOC && first == 0);
    fail_allocation = 0;
    bytes[12] = 1;
    const uint32_t previous_allocations = allocations;
    assert(hel_texture_create_native(bytes,56+layout.byte_length,3,5,&first) == HEL_ERR_MESH_INVALID);
    assert(allocations == previous_allocations);
    free(bytes);
    hel_texture_release_all(); hel_texture_release_all();
    assert(allocations == frees && hel_texture_count() == 0);
}
int main(void) {
    verify_formats_and_payloads(); verify_lifetime_and_legacy();
    puts("Xenos native texture tests PASS: 44 formats, strict header/fetch, direct uploads, legacy font alpha and fenced retirement.");
    return 0;
}
