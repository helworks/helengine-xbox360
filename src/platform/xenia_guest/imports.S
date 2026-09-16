/*
 * xboxkrnl.exe import records for the XEX2 import table (same layout as the
 * probe; see probes/entry/imports.S and docs/xenia-compatibility.md). Each
 * import is a 4-byte variable record (type 0 | ordinal) plus a 16-byte thunk
 * (type 1 | ordinal) that the loader rewrites to `sc 2; blr; nop; nop`.
 */
    .macro HEL_IMPORT name, ordinal
    .section .xeximp.var,"aw",@progbits
    .balign 4
    .globl __imp_\name
__imp_\name:
    .long \ordinal
    .section .xeximp.thunk,"ax",@progbits
    .balign 16
    .globl \name
    .type \name, @function
\name:
    .long 0x01000000 + \ordinal
    .long 0x60000000
    .long 0x60000000
    .long 0x60000000
    .endm

#include "imports.inc"
