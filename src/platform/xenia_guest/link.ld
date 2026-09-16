/*
 * Link layout for the helengine Xenia guest image (xenia-canary f21ebd49e).
 * Identical rules to probes/entry/link.ld: base 0x82000000 in the 64 KiB-page
 * xex heap, 4 KiB reserved for the PE headers written by tools/xexpack.py,
 * import thunks first, code and read-only data in the leading CODE pages,
 * writable data starting on the next 64 KiB page.
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
    KEEP(*(.xeximp.thunk))
    KEEP(*(.text.start))
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  . = ALIGN(HEL_PAGE_SIZE);

  /* Static constructors of the C++ core, run by hel_cxx_init. GCC marks
     .init_array writable, so it belongs in the writable pages, not with the
     code: tools/xexpack.py refuses a page that is executable and writable.
     The image is linked at a fixed base, so the pointers need no relocation. */
  .init_array : {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.init_array .ctors))
    PROVIDE_HIDDEN(__init_array_end = .);
  }

  /* Kept so the layout stays explicit; static destructors never run. */
  .fini_array : {
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
    KEEP(*(.fini_array .dtors))
  }

  .data : {
    *(.data .data.*)
    *(.data.rel.ro .data.rel.ro.*)
    /* libsupc++'s type-info objects land in .sdata. They are addressed
       absolutely (no small-data-relative relocations survive the link), so
       they only need a home, not a small-data base register: r13 stays the
       KPCR the emulator hands us. */
    *(.sdata .sdata.*)
    *(.xeximp.var)
  }

  .bss : {
    *(.sbss .sbss.*)
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
