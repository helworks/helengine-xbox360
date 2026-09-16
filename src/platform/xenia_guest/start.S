/*
 * Guest entry and assembly helpers for the Xenia XEX2 launch path
 * (xenia-canary f21ebd49e; see docs/xenia-compatibility.md section 2).
 *
 * Entry state: r1 = thread stack (176 bytes already reserved by the
 * emulator), r3 = 0, lr = host return sentinel, r13 = KPCR (never touched;
 * C code is compiled with -msdata=none). Register numbers are used because
 * the assembler runs without -mregnames.
 *
 * Every function ends with a `blr` that the emulator's function scanner can
 * find; import thunks are linked before this file (link.ld).
 */
    .section .text.start,"ax",@progbits
    .globl _start
    .type _start, @function
_start:
    mflr    0
    stw     0, 4(1)
    stwu    1, -32(1)
    bl      hel_demo_main
    /* hel_demo_main returns only after a failure or a frame limit; keep the
       emulator alive so the log and window can be inspected. */
1:  b       1b
    li      3, 0
    addi    1, 1, 32
    lwz     0, 4(1)
    mtlr    0
    blr
    .size _start, .-_start

    /* void hel_halt(void): stops after a fatal error. The trailing blr is
       unreachable but keeps the emulator's function scanner from merging the
       next function into this one (docs/xenia-compatibility.md section 3). */
    .globl hel_halt
    .type hel_halt, @function
hel_halt:
1:  b       1b
    blr
    .size hel_halt, .-hel_halt

    /* void hel_syscall_marker(uint32_t value): `sc` (LEV=0) makes the host log
       "Unhandled syscall <r0>", a kernel-independent execution marker. */
    .globl hel_syscall_marker
    .type hel_syscall_marker, @function
hel_syscall_marker:
    mr      0, 3
    sc
    blr
    .size hel_syscall_marker, .-hel_syscall_marker

    /* uint64_t hel_read_timebase(void): 64-bit time base in r3:r4. */
    .globl hel_read_timebase
    .type hel_read_timebase, @function
hel_read_timebase:
1:  mftbu   3
    mftb    4
    mftbu   5
    cmpw    3, 5
    bne     1b
    blr
    .size hel_read_timebase, .-hel_read_timebase

    /* hel_vdswap(a1..a10): SVR4 places a9 at 8(r1) and a10 at 12(r1) of the
       caller frame. The kernel export shim reads argument n (n >= 9) at
       r1 + 0x54 + 8*(n-9) as a big-endian dword, so copy them there in a
       fresh frame and call the VdSwap import thunk. */
    .globl hel_vdswap
    .type hel_vdswap, @function
hel_vdswap:
    mflr    0
    stw     0, 4(1)
    lwz     11, 8(1)
    lwz     12, 12(1)
    stwu    1, -0x70(1)
    stw     11, 0x54(1)
    stw     12, 0x5C(1)
    bl      VdSwap
    addi    1, 1, 0x70
    lwz     0, 4(1)
    mtlr    0
    blr
    .size hel_vdswap, .-hel_vdswap
