/*
 * xboxkrnl.exe import records for the XEX2 import table.
 *
 * Layout consumed by XexModule::SetupLibraryImports in xenia-canary f21ebd49e:
 * the import table lists guest addresses; the dword at each address encodes
 * (type << 24) | ordinal. Type 0 is a 4-byte variable record (overwritten with
 * 0xDEADC0DE for function exports). Type 1 is a 16-byte thunk that the loader
 * rewrites to `sc 2; blr; nop; nop`, and `sc 2` dispatches to the host export.
 * The pair for one function must be listed as variable record then thunk.
 *
 * tools/xexpack.py reads imports.inc and the nm output to build the table.
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
