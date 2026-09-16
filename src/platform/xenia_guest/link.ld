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
