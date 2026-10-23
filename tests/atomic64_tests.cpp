#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
#include <vector>

// Call the out-of-line ABI even on hosts whose compiler supports native atomics.
extern "C" std::uint64_t GuestFetchAdd8(volatile void*, std::uint64_t, int)
    __asm__("__atomic_fetch_add_8");

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void CheckValuesAndOrders() {
    const int orders[] = {__ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
        __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST};
    for (int order : orders) {
        alignas(8) std::uint64_t value = UINT64_C(0xffffffff);
        Require(GuestFetchAdd8(&value, 2, order) == UINT64_C(0xffffffff), "return old value across 32-bit carry");
        Require(value == UINT64_C(0x100000001), "preserve upper 32 bits");
        value = std::numeric_limits<std::uint64_t>::max();
        Require(GuestFetchAdd8(&value, 1, order) == std::numeric_limits<std::uint64_t>::max(), "return old value at wrap");
        Require(value == 0, "unsigned 64-bit wrap");
    }
}

static void CheckConcurrentTickets() {
    constexpr unsigned threadCount = 8;
    constexpr unsigned ticketsPerThread = 20000;
    constexpr std::uint64_t base = UINT64_C(0xffffff00);
    alignas(8) std::uint64_t next = base;
    alignas(8) std::uint64_t second = 0;
    std::vector<std::uint64_t> tickets(threadCount * ticketsPerThread);
    std::vector<std::thread> threads;
    for (unsigned thread = 0; thread < threadCount; ++thread) {
        threads.emplace_back([&, thread] {
            for (unsigned i = 0; i < ticketsPerThread; ++i) {
                tickets[thread * ticketsPerThread + i] = GuestFetchAdd8(&next, 1, __ATOMIC_RELAXED);
                GuestFetchAdd8(&second, UINT64_C(0x100000001), __ATOMIC_SEQ_CST);
            }
        });
    }
    for (auto& thread : threads) thread.join();
    std::sort(tickets.begin(), tickets.end());
    for (std::size_t i = 0; i < tickets.size(); ++i) {
        Require(tickets[i] == base + i, "every concurrent ticket is unique and contiguous");
    }
    Require(next == base + tickets.size(), "concurrent total has no lost updates");
    Require(second == tickets.size() * UINT64_C(0x100000001), "independent address preserves all 64 bits");
}

static void CheckReleaseAcquirePublication() {
    alignas(8) std::uint64_t ready = 0;
    std::uint64_t payload = 0;
    constexpr std::uint64_t iterations = 20000;
    std::thread writer([&] {
        for (std::uint64_t i = 1; i <= iterations; ++i) {
            while (GuestFetchAdd8(&ready, 0, __ATOMIC_ACQUIRE) != 0) std::this_thread::yield();
            payload = i * UINT64_C(0x100000001);
            GuestFetchAdd8(&ready, 1, __ATOMIC_RELEASE);
        }
    });
    std::thread reader([&] {
        for (std::uint64_t i = 1; i <= iterations; ++i) {
            while (GuestFetchAdd8(&ready, 0, __ATOMIC_ACQUIRE) != 1) std::this_thread::yield();
            Require(payload == i * UINT64_C(0x100000001), "release/acquire publishes non-atomic payload");
            GuestFetchAdd8(&ready, std::numeric_limits<std::uint64_t>::max(), __ATOMIC_RELEASE);
        }
    });
    writer.join();
    reader.join();
}

static void CheckSequentialConsistencyWithOtherAtomics() {
    // Store buffering: seq_cst forbids both readers observing the initial zero,
    // even when the other object uses the compiler's native 32-bit operation.
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        alignas(8) std::uint64_t first = 0;
        std::atomic<std::uint32_t> second{0};
        std::atomic<bool> start{false};
        std::uint64_t observedFirst = 1;
        std::uint32_t observedSecond = 1;
        std::thread left([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            GuestFetchAdd8(&first, 1, __ATOMIC_SEQ_CST);
            observedSecond = second.load(std::memory_order_seq_cst);
        });
        std::thread right([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            second.store(1, std::memory_order_seq_cst);
            observedFirst = GuestFetchAdd8(&first, 0, __ATOMIC_SEQ_CST);
        });
        start.store(true, std::memory_order_release);
        left.join();
        right.join();
        Require(observedFirst != 0 || observedSecond != 0, "seq_cst orders fetch-add with native atomic objects");
    }
}

int main() {
    CheckValuesAndOrders();
    CheckConcurrentTickets();
    CheckReleaseAcquirePublication();
    CheckSequentialConsistencyWithOtherAtomics();
    std::puts("4/4 atomic64 tests passed (six memory orders, 8 concurrent threads, publication, mixed-atomic seq_cst).");
}
