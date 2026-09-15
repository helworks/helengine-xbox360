/*
 * Link layout for the Xenia XEX2 guest image (xenia-canary f21ebd49e).
 *
 * The image base 0x82000000 lies in the 64 KiB-page "xex" heap
 * (0x80000000-0x8FFFFFFF) and above the ranges Xenia reserves for kernel
 * trampolines (0x80040000-0x801C0000) and kernel variables
 * (0x80070000-0x80280000). The first 4 KiB are left for the PE headers that
 * tools/xexpack.py writes; XexModule::ReadPEHeaders reads them at the base.
 * Code and read-only data occupy the leading 64 KiB pages (marked CODE in the
 * XEX page descriptors, protected read-only after load); writable data starts
 * on the next 64 KiB page (marked DATA).
 */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_start)

HEL_IMAGE_BASE = 0x82000000;
HEL_PAGE_SIZE = 0x10000;
HEL_HEADER_SIZE = 0x1000;

SECTIONS
{
  . = HEL_IMAGE_BASE + HEL_HEADER_SIZE;

  .text : {
    /* Import thunks first: each is `sc 2; blr`, so no preceding function
       without a `blr` can be merged into them by Xenia's function scanner. */
    KEEP(*(.xeximp.thunk))
    KEEP(*(.text.start))
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  . = ALIGN(HEL_PAGE_SIZE);

  .data : {
    *(.data .data.*)
    *(.xeximp.var)
  }

  .bss : {
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
  }

  /DISCARD/ : {
    *(.comment)
    *(.note .note.*)
    *(.eh_frame)
    *(.gnu.attributes)
  }
}
