/*
 * Guest side of the generated core's provider contract: the failure hook,
 * the monotonic clock, and definitions for the libstdc++ throw helpers.
 *
 * The throw helpers matter more than they look. libstdc++'s containers call
 * std::__throw_length_error and friends on error paths. The library's own
 * definitions throw, which would link the exception allocator, the terminate
 * handler, the personality routine and the unwinder into an image built with
 * -fno-exceptions. Defining them here keeps all of that out and turns those
 * paths into a logged stop, which is the correct behaviour for a console
 * image with no way to recover.
 */
#include <cstddef>

#include "X360RuntimeProvider.hpp"
#include "xenia_guest.h"

extern "C" {
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
unsigned long long hel_ticks(void);
unsigned long long hel_tick_frequency(void);
}

namespace he_cpp_custom {

void Fail(const char *message) {
    hel_log("HEL core: managed runtime failure");
    hel_log(message ? message : "(no message)");
    hel_heap_report("HEL core: heap state at failure");
    hel_halt();
    __builtin_unreachable();
}

std::uint64_t MonotonicMicroseconds() {
    std::uint64_t frequency = hel_tick_frequency();
    if (frequency == 0) {
        return 0;
    }
    /* Split the conversion so a long uptime cannot overflow the numerator:
       whole seconds first, then the remainder scaled to microseconds. */
    std::uint64_t ticks = hel_ticks();
    std::uint64_t seconds = ticks / frequency;
    std::uint64_t rest = ticks - seconds * frequency;
    return seconds * 1000000ull + (rest * 1000000ull) / frequency;
}

} // namespace he_cpp_custom

namespace {

[[noreturn]] void fail_without_unwinding(const char *what) {
    hel_log("HEL core: C++ library error path reached");
    hel_log(what);
    hel_heap_report("HEL core: heap state at failure");
    hel_halt();
    __builtin_unreachable();
}

} // namespace

/* Definitions for the helpers declared in <bits/functexcept.h>. Signatures
   must match the library's declarations exactly. */
namespace std {

void __throw_bad_alloc() { fail_without_unwinding("bad allocation"); }
void __throw_bad_array_new_length() {
    fail_without_unwinding("bad array new length");
}
void __throw_bad_function_call() {
    fail_without_unwinding("call through an empty function object");
}
void __throw_bad_cast() { fail_without_unwinding("bad cast"); }
void __throw_bad_typeid() { fail_without_unwinding("bad typeid"); }
void __throw_length_error(const char *what) { fail_without_unwinding(what); }
void __throw_logic_error(const char *what) { fail_without_unwinding(what); }
void __throw_domain_error(const char *what) { fail_without_unwinding(what); }
void __throw_invalid_argument(const char *what) { fail_without_unwinding(what); }
void __throw_out_of_range(const char *what) { fail_without_unwinding(what); }
void __throw_out_of_range_fmt(const char *what, ...) {
    fail_without_unwinding(what);
}
void __throw_runtime_error(const char *what) { fail_without_unwinding(what); }
void __throw_range_error(const char *what) { fail_without_unwinding(what); }
void __throw_overflow_error(const char *what) { fail_without_unwinding(what); }
void __throw_underflow_error(const char *what) { fail_without_unwinding(what); }
void __throw_ios_failure(const char *what) { fail_without_unwinding(what); }
void __throw_system_error(int) { fail_without_unwinding("system error"); }
void __throw_future_error(int) { fail_without_unwinding("future error"); }

} // namespace std
