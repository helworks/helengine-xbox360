/*
 * The C library entry points libstdc++ and libsupc++ still reference once the
 * transpiled core is linked in.
 *
 * Nothing here implements a C library for general use. Each function is the
 * smallest correct behaviour for a single-threaded console image:
 *
 *   malloc/free/calloc/realloc  the guest heap, so every allocation the C++
 *                               library makes is counted with the rest
 *   abort                       reports and stops, rather than vanishing
 *   atexit                      accepted and dropped: the image never exits
 *   getenv                      no environment exists, so always null
 *   strchr/strtoul              small correct implementations
 *   pthread_*                   single-threaded equivalents; the guest has
 *                               one thread, so a mutex cannot be contended
 *                               and "once" is a plain flag
 *
 * These are referenced from library code paths (the exception allocator, the
 * thread-support shims, locale setup) that this image is unlikely to reach.
 * They exist so the link is closed and so a path that does run reports
 * itself instead of jumping into nothing.
 */
#include <pthread.h>
#include <stddef.h>

extern "C" {
void *hel_malloc(unsigned int size);
void *hel_realloc(void *pointer, unsigned int size);
void hel_free(void *pointer);
void hel_log(const char *message);
void hel_halt(void);
}

extern "C" {

void *malloc(size_t size) { return hel_malloc((unsigned int)size); }

void free(void *pointer) { hel_free(pointer); }

void *calloc(size_t count, size_t size) {
    size_t total = count * size;
    if (count != 0 && total / count != size) {
        return nullptr; /* overflow */
    }
    void *block = hel_malloc((unsigned int)total);
    if (block) {
        unsigned char *bytes = (unsigned char *)block;
        for (size_t i = 0; i < total; ++i) {
            bytes[i] = 0;
        }
    }
    return block;
}

void *realloc(void *pointer, size_t size) {
    return hel_realloc(pointer, (unsigned int)size);
}

void abort(void) {
    hel_log("HEL libc: abort() called by the C++ library");
    hel_halt();
    __builtin_unreachable();
}

int atexit(void (*function)(void)) {
    (void)function;
    return 0;
}

char *getenv(const char *name) {
    (void)name;
    return nullptr;
}

char *strchr(const char *text, int character) {
    char wanted = (char)character;
    for (;; ++text) {
        if (*text == wanted) {
            return (char *)text;
        }
        if (*text == 0) {
            return nullptr;
        }
    }
}

unsigned long strtoul(const char *text, char **end, int base) {
    const char *cursor = text;
    while (*cursor == ' ' || (*cursor >= 9 && *cursor <= 13)) {
        cursor++;
    }
    if (base == 0) {
        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        } else if (cursor[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' &&
               (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    unsigned long value = 0;
    for (;; ++cursor) {
        int digit;
        if (*cursor >= '0' && *cursor <= '9') {
            digit = *cursor - '0';
        } else if (*cursor >= 'a' && *cursor <= 'z') {
            digit = *cursor - 'a' + 10;
        } else if (*cursor >= 'A' && *cursor <= 'Z') {
            digit = *cursor - 'A' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        value = value * (unsigned long)base + (unsigned long)digit;
    }
    if (end) {
        *end = (char *)cursor;
    }
    return value;
}

/* Single-threaded thread support. The keys table is small because the only
   users are the C++ library's own internals. */
namespace {
constexpr unsigned kMaxKeys = 16;
void *g_key_values[kMaxKeys];
bool g_key_used[kMaxKeys];
} // namespace

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    (void)destructor;
    for (unsigned i = 0; i < kMaxKeys; ++i) {
        if (!g_key_used[i]) {
            g_key_used[i] = true;
            g_key_values[i] = nullptr;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return 11; /* EAGAIN: out of keys */
}

int pthread_key_delete(pthread_key_t key) {
    if ((unsigned)key >= kMaxKeys) {
        return 22; /* EINVAL */
    }
    g_key_used[(unsigned)key] = false;
    g_key_values[(unsigned)key] = nullptr;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    return (unsigned)key < kMaxKeys ? g_key_values[(unsigned)key] : nullptr;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if ((unsigned)key >= kMaxKeys) {
        return 22; /* EINVAL */
    }
    g_key_values[(unsigned)key] = const_cast<void *>(value);
    return 0;
}

int pthread_once(pthread_once_t *control, void (*routine)(void)) {
    if (!control || !routine) {
        return 22; /* EINVAL */
    }
    /* newlib's pthread_once_t is a single status word: 0 not run, 1 running,
       2 finished. */
    if (control->status == 0) {
        control->status = 1;
        routine();
        control->status = 2;
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

} // extern "C"
