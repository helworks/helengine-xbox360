/*
 * Minimal guest execution probe for Xenia.
 *
 * Freestanding C: no libc, no runtime. The only external calls are the
 * xboxkrnl.exe import thunk DbgPrint (resolved by the XEX2 loader) and the
 * `sc` marker implemented in start.S. Both are observation channels; the
 * probe's intended behaviour is the marker store followed by an infinite loop.
 */
#include <stdint.h>

volatile unsigned int hel_probe_result = 0;

/* xboxkrnl.exe ordinal 3, thunk emitted by imports.S. */
extern uint32_t DbgPrint(const char *format, ...);

/* start.S: executes `sc` with r0 = value; the pinned Xenia logs the value. */
extern void hel_syscall_marker(uint32_t value);

/*
 * Returns to start.S, which then loops forever. The loop is not written here
 * because Xenia's function scanner (src/xenia/cpu/ppc/ppc_scanner.cc) only
 * ends a function at a `blr`; a C `for (;;) {}` compiles to a self-branch
 * without a `blr`, so the scanner would run on into the following code.
 */
void hel_probe_main(void) {
    hel_probe_result = 0x48454C30u; /* "HEL0" */
    unsigned int readback = hel_probe_result;
    hel_syscall_marker(readback);
    DbgPrint("HEL probe: result=%08X at %08X\n", readback,
             (unsigned int)(uintptr_t)&hel_probe_result);
}
