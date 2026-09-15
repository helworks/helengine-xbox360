/*
 * Guest entry for the Xenia XEX2 launch path (xenia-canary f21ebd49e).
 *
 * KernelState::LaunchModule creates the main XThread with the module entry
 * point and start_context 0. Processor::Execute then pads r1 by 176 bytes,
 * sets lr to a host return sentinel and calls the entry with r3 = 0.
 * ThreadState initialises r1 = stack base, r2 = 0x20000000, r13 = KPCR.
 * r2 and r13 are never touched by this code (compiled with -msdata=none).
 * Register numbers are used because the assembler is run without -mregnames.
 */
    .section .text.start,"ax",@progbits
    .globl _start
    .type _start, @function
_start:
    mflr    0
    stw     0, 4(1)
    stwu    1, -32(1)
    bl      hel_probe_main
    /* The probe intentionally loops forever after reporting its marker. */
1:  b       1b
    /* Unreachable, but Xenia's function scanner ends functions only at a
       `blr`; without it the scan would continue into the next function. */
    li      3, 0
    addi    1, 1, 32
    lwz     0, 4(1)
    mtlr    0
    blr
    .size _start, .-_start

    /* void hel_syscall_marker(uint32_t value): `sc` with LEV=0 reaches the
       host SyscallHandler, which logs "Unhandled syscall <r0>". */
    .globl hel_syscall_marker
    .type hel_syscall_marker, @function
hel_syscall_marker:
    mr      0, 3
    sc
    blr
    .size hel_syscall_marker, .-hel_syscall_marker
