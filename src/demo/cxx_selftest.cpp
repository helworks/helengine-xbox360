/*
 * Proves the guest's C++ environment before the transpiled engine core is
 * linked in, and stays as a regression check afterwards.
 *
 * It exercises exactly what the generated core relies on and nothing else:
 * a static constructor running before main, virtual dispatch through a base
 * pointer, heap allocation and release through new/delete, arrays with
 * element destructors, a function-local static, and 64-bit and
 * floating-point arithmetic through libgcc. Every check logs PASS or FAIL;
 * a failure also raises the failure count that hel_cxx_selftest returns, so
 * a caller can stop before drawing anything.
 */
#include <stdint.h>

#include "../engine/frame.h"
#include "../platform/xenia_guest/xenia_guest.h"

namespace {

uint32_t g_failures;

/* Logs "<what>=00000001" for a pass and "=00000000" for a failure, so a
   failing check is visible in the emulator log without a second line. */
void check(const char *what, bool ok) {
    hel_log_u32(what, ok ? 1u : 0u);
    if (!ok) {
        g_failures++;
    }
}

/* A namespace-scope object with a constructor: this must have run before
   hel_cxx_selftest is entered, which is what .init_array delivers. */
struct StaticProbe {
    StaticProbe() : marker(0xC0FFEEu) {}
    uint32_t marker;
};
StaticProbe g_static_probe;

struct Shape {
    explicit Shape(uint32_t sides) : sides_(sides) {}
    virtual ~Shape() {}
    virtual uint32_t corners() const { return sides_; }
    uint32_t sides_;
};

struct Square : Shape {
    Square() : Shape(4) {}
    uint32_t corners() const override { return sides_ * 2u; }
};

uint32_t g_destroyed;

struct Counted {
    ~Counted() { g_destroyed++; }
    uint32_t value = 7u;
};

/* GCC marks the replaceable global operator new as malloc-like, which means
   it may move a read of a global counter across the allocation. Every
   measurement around new or delete is fenced so the check measures the
   runtime rather than the optimiser's freedom. */
void barrier() { __asm__ __volatile__("" ::: "memory"); }

uint32_t local_static_value() {
    static uint32_t value = 0;
    value += 5u;
    return value;
}

} // namespace

extern "C" uint32_t hel_cxx_selftest(void) {
    g_failures = 0;
    hel_log("HEL cxx: self-test start");

    check("HEL cxx: static constructor ran",
          g_static_probe.marker == 0xC0FFEEu);

    Square square;
    Shape *shape = &square;
    check("HEL cxx: virtual dispatch", shape->corners() == 8u);

    uint32_t before = hel_heap_live_allocations();
    uint32_t allocations_before = hel_heap_total_allocations();
    barrier();
    Shape *heap_shape = new Square();
    barrier();
    check("HEL cxx: new used the guest heap",
          heap_shape != 0 &&
              hel_heap_total_allocations() == allocations_before + 1u &&
              hel_heap_live_allocations() == before + 1u);
    check("HEL cxx: virtual dispatch on a heap object",
          heap_shape->corners() == 8u);
    delete heap_shape;
    barrier();
    check("HEL cxx: delete released",
          hel_heap_live_allocations() == before);

    g_destroyed = 0;
    barrier();
    Counted *array = new Counted[4];
    barrier();
    check("HEL cxx: array new", array != 0 && array[3].value == 7u);
    delete[] array;
    check("HEL cxx: array delete ran destructors", g_destroyed == 4u);

    check("HEL cxx: function-local static",
          local_static_value() == 5u && local_static_value() == 10u);

    /* libgcc paths the generated core uses: 64-bit division and remainder
       are compiler-runtime calls on this 32-bit target, checked against the
       division identity rather than a hand-computed constant. */
    const uint64_t numerator = 0x123456789ABCDEF0ull;
    uint64_t quotient = numerator / 7ull;
    uint64_t remainder = numerator % 7ull;
    check("HEL cxx: 64-bit division",
          quotient * 7ull + remainder == numerator && remainder < 7ull &&
              quotient != 0ull);
    volatile float f = 2.5f;
    check("HEL cxx: float arithmetic", (uint32_t)(f * 4.0f) == 10u);

    check("HEL cxx: heap survived the self-test",
          hel_heap_failed_allocations() == 0u);

    hel_log_u32("HEL cxx: self-test failures", g_failures);
    return g_failures;
}
