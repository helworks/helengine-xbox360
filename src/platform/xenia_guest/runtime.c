/*
 * Minimal freestanding runtime. GCC may emit calls to memset/memcpy/memmove
 * for aggregate initialisation and struct copies even with -fno-builtin, and
 * no C library is linked, so these are provided here.
 *
 * The string functions are here for the same reason: libsupc++'s run-time
 * type information compares type names with strcmp, and the transpiled
 * engine core's generated string code uses the rest.
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

int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

void *memchr(const void *block, int value, size_t count) {
    const uint8_t *bytes = (const uint8_t *)block;
    for (size_t i = 0; i < count; ++i) {
        if (bytes[i] == (uint8_t)value) {
            return (void *)(bytes + i);
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return (int)(uint8_t)*left - (int)(uint8_t)*right;
}

int strncmp(const char *left, const char *right, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) {
            return (int)(uint8_t)left[i] - (int)(uint8_t)right[i];
        }
        if (left[i] == 0) {
            break;
        }
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *out = dest;
    while ((*out++ = *src++) != 0) {
    }
    return dest;
}

/* newlib's math library reports domain errors through errno. The guest is
   single threaded and never reads it, but the symbol has to exist. */
static int g_errno;

int *__errno(void) { return &g_errno; }
