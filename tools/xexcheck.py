#!/usr/bin/env python3
"""Static checks for a guest ELF and its XEX2 wrapper against the pinned
xenia-canary (f21ebd49e) loader rules. Exit status 0 means every rule passed.

Rules checked (source references in docs/xenia-compatibility.md):
- ELF: ELFCLASS32, ELFDATA2MSB, EM_PPC, ET_EXEC, non-zero entry, PT_LOAD
  segments inside the 64 KiB-page xex heap (0x80000000-0x8FFFFFFF) and outside
  the kernel trampoline/variable ranges (0x80040000-0x80280000).
- ELF symbols: required symbols defined, no undefined symbols, no small-data
  sections (.sdata/.sbss) that would rely on r13/r2.
- XEX2: magic, header size, security info, load address, page descriptors
  covering the image, PE header fields ReadPEHeaders verifies, import records
  inside the image with the encoded ordinal, entry and thunks in CODE pages.
"""

import argparse
import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
from xexpack import (ElfImage, parse_imports, parse_nm, SHF_ALLOC,  # noqa
                     XEX_SECTION_CODE)


class Check:
    def __init__(self):
        self.failures = 0

    def ok(self, condition, message):
        print(("PASS " if condition else "FAIL ") + message)
        if not condition:
            self.failures += 1


def check_elf(check, elf, nm_symbols, required):
    check.ok(elf.entry != 0, "entry point is non-zero (0x%08X)" % elf.entry)
    for segment in elf.segments:
        start = segment["vaddr"]
        end = start + segment["memsz"]
        check.ok(0x80000000 <= start and end <= 0x90000000,
                 "PT_LOAD 0x%08X-0x%08X inside the 64 KiB xex heap" %
                 (start, end))
        check.ok(end <= 0x80040000 or start >= 0x80280000,
                 "PT_LOAD 0x%08X-0x%08X avoids the kernel ranges" % (start, end))
    names = {s["name"] for s in elf.sections}
    check.ok(not ({".sdata", ".sbss", ".sdata2"} & names),
             "no small-data sections (r13/r2 must stay untouched)")
    for name in required:
        check.ok(name in nm_symbols, "symbol %s is defined" % name)
    undefined = [name for name, (_, kind) in nm_symbols.items() if kind == "U"]
    check.ok(not undefined, "no undefined symbols (%s)" % ", ".join(undefined))


def check_xex(check, data, elf, nm_symbols, imports):
    check.ok(data[:4] == b"XEX2", "XEX2 magic")
    (module_flags, header_size, _reserved, security_offset,
     header_count) = struct.unpack_from(">IIIII", data, 4)
    check.ok(module_flags & 1, "module_flags has XEX_MODULE_TITLE")
    check.ok(header_size < len(data), "header_size %d inside file" % header_size)
    (sec_size, image_size, ) = struct.unpack_from(">II", data, security_offset)
    load_address = struct.unpack_from(">I", data, security_offset + 0x110)[0]
    descriptor_count = struct.unpack_from(">I", data, security_offset + 0x180)[0]
    check.ok(load_address == 0x82000000, "load address 0x%08X" % load_address)
    check.ok(len(data) - header_size == image_size,
             "base file length %d equals image_size %d" %
             (len(data) - header_size, image_size))
    pages = 0
    code_pages = set()
    for index in range(descriptor_count):
        value = struct.unpack_from(
            ">I", data, security_offset + 0x184 + index * 24)[0]
        info, count = value & 0xF, value >> 4
        if info == XEX_SECTION_CODE:
            code_pages.update(range(pages, pages + count))
        pages += count
    check.ok(pages * 0x10000 == image_size,
             "page descriptors cover the image (%d pages)" % pages)
    image = data[header_size:]
    check.ok(struct.unpack_from("<I", image, 0)[0] == 0x905A4D,
             "PE signature dword 0x905A4D")
    pe_offset = struct.unpack_from("<i", image, 60)[0]
    check.ok(image[pe_offset:pe_offset + 4] == b"PE\0\0", "PE\\0\\0 signature")
    machine, _sections, _ts, _sym, _nsym, opt_size, characteristics = (
        struct.unpack_from("<HHIIIHH", image, pe_offset + 4))
    check.ok(machine == 0x01F2, "PE machine POWERPCBE")
    check.ok(characteristics & 0x0100, "PE 32BIT_MACHINE characteristic")
    check.ok(opt_size == 224, "PE optional header size 224")
    magic, = struct.unpack_from("<H", image, pe_offset + 24)
    subsystem, = struct.unpack_from("<H", image, pe_offset + 24 + 68)
    check.ok(magic == 0x10B, "PE optional header magic 0x10B")
    check.ok(subsystem == 14, "PE subsystem XBOX (14)")

    def page_of(address):
        return (address - load_address) // 0x10000

    check.ok(page_of(elf.entry) in code_pages, "entry point in a CODE page")
    entry_header = None
    for index in range(header_count):
        key, value = struct.unpack_from(">II", data, 0x18 + 8 * index)
        if key == 0x00010100:
            entry_header = value
    check.ok(entry_header == elf.entry, "XEX entry header matches ELF entry")
    for name, ordinal in imports:
        var_addr = nm_symbols["__imp_" + name][0]
        thunk_addr = nm_symbols[name][0]
        var_value = struct.unpack_from(">I", image, var_addr - load_address)[0]
        thunk_value = struct.unpack_from(">I", image,
                                         thunk_addr - load_address)[0]
        check.ok(var_value == ordinal and thunk_value == 0x01000000 | ordinal,
                 "import %s (ordinal %d) records encoded" % (name, ordinal))
        check.ok(page_of(thunk_addr) in code_pages,
                 "import %s thunk in a CODE page" % name)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--elf", required=True)
    parser.add_argument("--xex", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--imports", required=True)
    parser.add_argument("--require", action="append", default=[],
                        help="symbol that must be defined")
    args = parser.parse_args(argv)
    with open(args.elf, "rb") as handle:
        elf = ElfImage(handle.read())
    with open(args.xex, "rb") as handle:
        xex = handle.read()
    with open(args.nm, "r", encoding="ascii") as handle:
        nm_symbols = parse_nm(handle.read())
    with open(args.imports, "r", encoding="ascii") as handle:
        imports = parse_imports(handle.read())
    check = Check()
    check_elf(check, elf, nm_symbols, args.require)
    check_xex(check, xex, elf, nm_symbols, imports)
    print("%d failure(s)" % check.failures)
    return 1 if check.failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
