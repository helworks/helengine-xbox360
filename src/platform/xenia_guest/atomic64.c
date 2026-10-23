/*
 * GCC's out-of-line 64-bit fetch-add ABI for the 32-bit PowerPC guest.
 * devkitPPC has native 32-bit atomics but supplies no libatomic for this ABI.
 * Keep the complete 64-bit counter and serialize read/modify/write with a
 * process-wide 32-bit lock. This operation is blocking, not lock-free, and
 * must not be called recursively from an interrupt or signal handler.
 */
#include <stdint.h>

_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
    "The atomic64 lock requires native 32-bit atomics.");

static uint32_t atomic64_lock;

/* Use an assembler name to provide the GCC ABI without redeclaring a builtin. */
uint64_t x360_atomic_fetch_add_8(volatile void *address, uint64_t increment,
    int memory_order) __asm__("__atomic_fetch_add_8");

uint64_t x360_atomic_fetch_add_8(volatile void *address, uint64_t increment,
    int memory_order) {
    /*
     * Intentionally strengthen every valid GCC memory order to seq_cst.
     * GCC explicitly permits this implementation strategy, including runtime
     * memory-order values. Sequentially consistent lock operations also order
     * surrounding accesses; acquire/release alone would not guarantee the
     * requested global seq_cst ordering relative to other atomic objects.
     * https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
     */
    (void)memory_order;
    while (__atomic_exchange_n(&atomic64_lock, 1u, __ATOMIC_SEQ_CST) != 0u) {
        while (__atomic_load_n(&atomic64_lock, __ATOMIC_RELAXED) != 0u) {
        }
    }

    volatile uint64_t *counter = (volatile uint64_t *)address;
    uint64_t previous = *counter;
    *counter = previous + increment;

    __atomic_store_n(&atomic64_lock, 0u, __ATOMIC_SEQ_CST);
    return previous;
}
