/*
 * Minimal freestanding runtime. GCC may emit calls to memset/memcpy/memmove
 * for aggregate initialisation and struct copies even with -fno-builtin, and
 * no C library is linked, so these are provided here.
 */
#include <stddef.h>
#include <stdint.h>

void *memset(void *dest, int value, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < count; ++i) {
        d[i] = (uint8_t)value;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < count; ++i) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = count; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}
