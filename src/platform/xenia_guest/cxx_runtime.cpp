/*
 * The C++ runtime support the guest must provide itself.
 *
 * The transpiled engine core is C++ compiled freestanding: no C++ standard
 * library, no exceptions, no RTTI (tools/build.ps1 passes -fno-exceptions
 * -fno-rtti -fno-threadsafe-statics). What the compiler still emits calls to
 * is defined here:
 *
 *   operator new / delete    -> the guest heap (heap.c)
 *   __cxa_atexit             -> accepted and dropped; static destructors do
 *                               not run, because the image never returns to a
 *                               host that would run them
 *   __cxa_pure_virtual       -> fatal, with a log line naming the cause
 *   hel_cxx_init             -> runs .init_array (static constructors)
 *
 * -fno-threadsafe-statics removes the __cxa_guard_* calls for function-local
 * statics; the guest is single-threaded, so the guards would only cost code.
 */
#include <stddef.h>
#include <stdint.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"

/* .init_array bounds from link.ld. */
extern "C" {
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
}

static void *allocate_or_fail(size_t size) {
    void *block = hel_malloc((uint32_t)size);
    if (!block) {
        /* A freestanding new cannot throw and callers do not check, so an
           exhausted heap is fatal and says so before stopping. */
        hel_log_u32("HEL cxx: operator new failed, bytes", (uint32_t)size);
        hel_heap_report("HEL cxx: heap state at failure");
        hel_halt();
    }
    return block;
}

void *operator new(size_t size) { return allocate_or_fail(size); }
void *operator new[](size_t size) { return allocate_or_fail(size); }
void operator delete(void *block) noexcept { hel_free(block); }
void operator delete[](void *block) noexcept { hel_free(block); }
void operator delete(void *block, size_t) noexcept { hel_free(block); }
void operator delete[](void *block, size_t) noexcept { hel_free(block); }

extern "C" {

/* Identifies this image to __cxa_atexit; its value is never inspected. */
void *__dso_handle = 0;

int __cxa_atexit(void (*destructor)(void *), void *object, void *dso) {
    (void)destructor;
    (void)object;
    (void)dso;
    return 0;
}

void __cxa_pure_virtual(void) {
    hel_log("HEL cxx: pure virtual function called");
    hel_halt();
}

void __cxa_deleted_virtual(void) {
    hel_log("HEL cxx: deleted virtual function called");
    hel_halt();
}

uint32_t hel_cxx_static_initialiser_count(void) {
    return (uint32_t)(__init_array_end - __init_array_start);
}

/* Runs static constructors. The heap must already exist: a constructor may
   allocate, and there is no way to order that against heap creation once the
   generated core is linked in. */
void hel_cxx_init(void) {
    uint32_t count = hel_cxx_static_initialiser_count();
    hel_log_u32("HEL cxx: static initialisers", count);
    for (uint32_t i = 0; i < count; ++i) {
        __init_array_start[i]();
    }
    hel_log("HEL cxx: static initialisers complete");
}
}
