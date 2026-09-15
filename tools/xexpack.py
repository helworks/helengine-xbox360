#!/usr/bin/env python3
"""Wrap a statically linked PowerPC ELF into an unencrypted, uncompressed XEX2.

The container layout is the one accepted by xenia-canary commit
f21ebd49e979e44f081f474df78c3fbfee9cb3f2:

- src/xenia/kernel/util/xex2_info.h      (header, security info, import table)
- src/xenia/cpu/xex_module.cc            (Load, ReadImageUncompressed,
                                          ReadPEHeaders, LoadContinue,
                                          SetupLibraryImports)
- src/xenia/base/pe_image.h              (PE header field layout)

The base file is the raw memory image of the module: PE headers at image
offset 0 followed by the linked sections at their link addresses. Xenia copies
it verbatim to the image base, reads the PE headers from there, applies the
page-descriptor protection and rewrites the import records.

Only the Python standard library is used. The tool is host-side and does not
run on the console or in the emulator.
"""

import argparse
import json
import re
import struct
import sys

ELF_MAGIC = b"\x7fELF"
EM_PPC = 20
ET_EXEC = 2
PT_LOAD = 1
PF_X = 1
PF_W = 2
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_NOBITS = 8

XEX_MODULE_TITLE = 0x00000001
XEX_HEADER_FILE_FORMAT_INFO = 0x000003FF
XEX_HEADER_ENTRY_POINT = 0x00010100
XEX_HEADER_IMAGE_BASE_ADDRESS = 0x00010201
XEX_HEADER_IMPORT_LIBRARIES = 0x000103FF
XEX_HEADER_DEFAULT_STACK_SIZE = 0x00020200
XEX_SECTION_CODE = 1
XEX_SECTION_DATA = 2
XEX_SECTION_READONLY_DATA = 3

PE_MACHINE_POWERPCBE = 0x01F2
PE_FILE_EXECUTABLE_IMAGE = 0x0002
PE_FILE_32BIT_MACHINE = 0x0100
PE_OPTIONAL_HDR32_MAGIC = 0x10B
PE_SIZEOF_OPTIONAL_HEADER = 224
PE_SUBSYSTEM_XBOX = 14
PE_SCN_CNT_CODE = 0x00000020
PE_SCN_CNT_INITIALIZED_DATA = 0x00000040
PE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
PE_SCN_MEM_EXECUTE = 0x20000000
PE_SCN_MEM_READ = 0x40000000
PE_SCN_MEM_WRITE = 0x80000000

KERNEL_LIBRARY = "xboxkrnl.exe"


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


class ElfImage:
    """Minimal ELF32 big-endian parser: program headers, sections, names."""

    def __init__(self, data):
        self.data = data
        if data[:4] != ELF_MAGIC:
            raise ValueError("not an ELF file")
        if data[4] != 1:
            raise ValueError("ELF class is not ELFCLASS32")
        if data[5] != 2:
            raise ValueError("ELF data encoding is not big-endian (ELFDATA2MSB)")
        (e_type, e_machine, _e_version, self.entry, e_phoff, e_shoff, _e_flags,
         _e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
         e_shstrndx) = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
        if e_type != ET_EXEC:
            raise ValueError("ELF is not ET_EXEC")
        if e_machine != EM_PPC:
            raise ValueError("ELF machine is not EM_PPC (20)")
        self.segments = []
        for index in range(e_phnum):
            offset = e_phoff + index * e_phentsize
            (p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, p_flags,
             p_align) = struct.unpack_from(">IIIIIIII", data, offset)
            if p_type == PT_LOAD:
                self.segments.append({
                    "offset": p_offset, "vaddr": p_vaddr, "filesz": p_filesz,
                    "memsz": p_memsz, "flags": p_flags, "align": p_align,
                })
        raw_sections = []
        for index in range(e_shnum):
            offset = e_shoff + index * e_shentsize
            (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, _link,
             _info, _addralign, _entsize) = struct.unpack_from(
                 ">IIIIIIIIII", data, offset)
            raw_sections.append((sh_name, sh_type, sh_flags, sh_addr, sh_offset,
                                 sh_size))
        names_offset = raw_sections[e_shstrndx][4] if e_shnum else 0
        self.sections = []
        for (sh_name, sh_type, sh_flags, sh_addr, sh_offset,
             sh_size) in raw_sections:
            end = data.index(b"\0", names_offset + sh_name)
            name = data[names_offset + sh_name:end].decode("ascii")
            self.sections.append({
                "name": name, "type": sh_type, "flags": sh_flags,
                "addr": sh_addr, "offset": sh_offset, "size": sh_size,
            })


def parse_nm(text):
    symbols = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = (int(parts[0], 16), parts[1])
    return symbols


def parse_imports(text):
    pattern = re.compile(r"^\s*HEL_IMPORT\s+(\w+)\s*,\s*(\d+)", re.MULTILINE)
    return [(name, int(ordinal)) for name, ordinal in pattern.findall(text)]


def build_pe_headers(elf, base, image_size, stack_size, header_size):
    alloc_sections = [s for s in elf.sections
                      if s["flags"] & SHF_ALLOC and s["size"]]
    dos = bytearray(64)
    struct.pack_into("<HHHHHHHHHHHHHH", dos, 0, 0x5A4D, 0x90, 3, 0, 4, 0,
                     0xFFFF, 0, 0xB8, 0, 0, 0, 0x40, 0)
    struct.pack_into("<i", dos, 60, 0x40)
    section_headers = bytearray()
    size_of_code = 0
    size_of_init = 0
    size_of_uninit = 0
    base_of_code = None
    base_of_data = None
    for section in alloc_sections:
        rva = section["addr"] - base
        flags = PE_SCN_MEM_READ
        if section["flags"] & SHF_EXECINSTR:
            flags |= PE_SCN_CNT_CODE | PE_SCN_MEM_EXECUTE
            size_of_code += section["size"]
            if base_of_code is None:
                base_of_code = rva
        elif section["type"] == SHT_NOBITS:
            flags |= PE_SCN_CNT_UNINITIALIZED_DATA | PE_SCN_MEM_WRITE
            size_of_uninit += section["size"]
        else:
            flags |= PE_SCN_CNT_INITIALIZED_DATA
            if section["flags"] & SHF_WRITE:
                flags |= PE_SCN_MEM_WRITE
            size_of_init += section["size"]
            if base_of_data is None:
                base_of_data = rva
        raw_size = 0 if section["type"] == SHT_NOBITS else section["size"]
        name = section["name"].encode("ascii")[:8].ljust(8, b"\0")
        section_headers += struct.pack("<8sIIIIIIHHI", name, section["size"],
                                       rva, raw_size, rva, 0, 0, 0, 0, flags)
    file_header = struct.pack("<HHIIIHH", PE_MACHINE_POWERPCBE,
                              len(alloc_sections), 0, 0, 0,
                              PE_SIZEOF_OPTIONAL_HEADER,
                              PE_FILE_EXECUTABLE_IMAGE | PE_FILE_32BIT_MACHINE)
    optional = struct.pack(
        "<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII",
        PE_OPTIONAL_HDR32_MAGIC, 0, 0, size_of_code, size_of_init,
        size_of_uninit, elf.entry - base, base_of_code or 0, base_of_data or 0,
        base, 0x1000, 0x1000, 0, 0, 0, 0, 0, 0, 0, image_size, header_size, 0,
        PE_SUBSYSTEM_XBOX, 0, stack_size, stack_size, 0, 0, 0, 16)
    optional += b"\0" * (16 * 8)
    assert len(optional) == PE_SIZEOF_OPTIONAL_HEADER
    headers = bytes(dos) + b"PE\0\0" + file_header + optional + bytes(
        section_headers)
    if len(headers) > header_size:
        raise ValueError("PE headers do not fit in the reserved header area")
    return headers.ljust(header_size, b"\0")


def classify_pages(elf, base, image_size, page_size):
    """Return [(info, page_count)] runs covering the whole image."""
    page_count = image_size // page_size
    infos = []
    for page in range(page_count):
        start = base + page * page_size
        end = start + page_size
        executable = page == 0
        writable = False
        for segment in elf.segments:
            seg_start = segment["vaddr"]
            seg_end = seg_start + segment["memsz"]
            if seg_start < end and seg_end > start:
                if segment["flags"] & PF_X:
                    executable = True
                if segment["flags"] & PF_W:
                    writable = True
        if executable and writable:
            raise ValueError(
                "page 0x%08X mixes executable and writable segments" % start)
        if executable:
            infos.append(XEX_SECTION_CODE)
        elif writable:
            infos.append(XEX_SECTION_DATA)
        else:
            infos.append(XEX_SECTION_READONLY_DATA)
    runs = []
    for info in infos:
        if runs and runs[-1][0] == info:
            runs[-1][1] += 1
        else:
            runs.append([info, 1])
    return [tuple(run) for run in runs]


def build_import_block(records):
    library_name = KERNEL_LIBRARY.encode("ascii") + b"\0"
    string_table = library_name.ljust(align_up(len(library_name), 4), b"\0")
    library = struct.pack(">I", 0x28 + 4 * len(records))
    library += b"\0" * 20                       # next_import_digest
    library += struct.pack(">III", 0, 0, 0)     # id, version, version_min
    library += struct.pack(">HH", 0, len(records))
    for address in records:
        library += struct.pack(">I", address)
    block = struct.pack(">II", len(string_table), 1) + string_table + library
    return struct.pack(">I", 4 + len(block)) + block


def build_security_info(base, image_size, page_runs, import_count):
    descriptors = b""
    for info, count in page_runs:
        # xex2_page_descriptor::value is declared xe::be<uint32_t>. Xenia reads
        # it as `desc.value = xe::byte_swap(descriptor.value)`, which swaps the
        # raw storage of the be<> wrapper, so the bit fields (info in bits 0-3,
        # page_count in bits 4-31) see the file's big-endian value directly.
        descriptors += struct.pack(">I", (count << 4) | info) + b"\0" * 20
    size = 0x184 + len(descriptors)
    info = struct.pack(">II", size, image_size)
    info += b"\0" * 0x100                        # rsa_signature
    info += struct.pack(">IIII", size, 0, base, 0)[:12]  # info_size, flags, load
    info += b"\0" * 20                           # section_digest
    info += struct.pack(">I", import_count)
    info += b"\0" * 20                           # import_table_digest
    info += b"\0" * 16                           # xgd2_media_id
    info += b"\0" * 16                           # aes_key (unused: no encryption)
    info += struct.pack(">I", 0)                 # export_table
    info += b"\0" * 20                           # header_digest
    info += struct.pack(">III", 0xFFFFFFFF, 0xFFFFFFFF, len(page_runs))
    assert len(info) == 0x184, len(info)
    return info + descriptors


def build_xex(elf, nm_symbols, imports, base, page_size, stack_size,
              header_size, report):
    image_end = max(s["vaddr"] + s["memsz"] for s in elf.segments)
    image_size = align_up(image_end - base, page_size)
    image = bytearray(image_size)
    for segment in elf.segments:
        # GNU ld folds the ELF file headers into the first PT_LOAD when they
        # share its page. Those bytes lie below base + header_size, where the
        # PE headers go, so only the part at or above that boundary is copied.
        seg_start = segment["vaddr"]
        seg_end = seg_start + segment["filesz"]
        copy_start = max(seg_start, base + header_size)
        if copy_start >= seg_end:
            continue
        file_offset = segment["offset"] + (copy_start - seg_start)
        image[copy_start - base:seg_end - base] = elf.data[
            file_offset:file_offset + (seg_end - copy_start)]
    image[:header_size] = build_pe_headers(elf, base, image_size, stack_size,
                                           header_size)
    if struct.unpack_from("<I", image, 0)[0] != 0x905A4D:
        raise ValueError("PE signature check (0x905A4D) would fail")

    page_runs = classify_pages(elf, base, image_size, page_size)

    records = []
    import_report = []
    for name, ordinal in imports:
        try:
            var_addr = nm_symbols["__imp_" + name][0]
            thunk_addr = nm_symbols[name][0]
        except KeyError as error:
            raise ValueError("import symbol missing from nm output: %s" % error)
        var_value = struct.unpack_from(">I", image, var_addr - base)[0]
        thunk_value = struct.unpack_from(">I", image, thunk_addr - base)[0]
        if var_value != ordinal or thunk_value != (0x01000000 | ordinal):
            raise ValueError("import record contents for %s do not match "
                             "ordinal %d" % (name, ordinal))
        records += [var_addr, thunk_addr]
        import_report.append({"name": name, "ordinal": ordinal,
                              "variable": "0x%08X" % var_addr,
                              "thunk": "0x%08X" % thunk_addr})

    import_block = build_import_block(records)
    file_format = struct.pack(">IHH", 8, 0, 0)
    security = build_security_info(base, image_size, page_runs, len(records))

    opt_headers = [
        (XEX_HEADER_FILE_FORMAT_INFO, "block", file_format),
        (XEX_HEADER_ENTRY_POINT, "value", elf.entry),
        (XEX_HEADER_IMAGE_BASE_ADDRESS, "value", base),
        (XEX_HEADER_IMPORT_LIBRARIES, "block", import_block),
        (XEX_HEADER_DEFAULT_STACK_SIZE, "value", stack_size),
    ]
    fixed_size = 0x18 + 8 * len(opt_headers)
    blocks = bytearray()
    entries = []
    for key, kind, payload in opt_headers:
        if kind == "value":
            entries.append((key, payload))
        else:
            entries.append((key, fixed_size + len(blocks)))
            blocks += payload
            blocks += b"\0" * (align_up(len(blocks), 4) - len(blocks))
    security_offset = fixed_size + len(blocks)
    header_total = align_up(security_offset + len(security), 0x1000)
    header = bytearray(header_total)
    struct.pack_into(">4sIIIII", header, 0, b"XEX2", XEX_MODULE_TITLE,
                     header_total, 0, security_offset, len(opt_headers))
    for index, (key, value) in enumerate(entries):
        struct.pack_into(">II", header, 0x18 + 8 * index, key, value)
    header[fixed_size:fixed_size + len(blocks)] = blocks
    header[security_offset:security_offset + len(security)] = security

    report.update({
        "image_base": "0x%08X" % base,
        "image_size": image_size,
        "entry_point": "0x%08X" % elf.entry,
        "header_size": header_total,
        "stack_size": stack_size,
        "page_size": page_size,
        "page_descriptors": [{"info": info, "pages": count}
                             for info, count in page_runs],
        "imports": import_report,
        "sections": [{"name": s["name"], "addr": "0x%08X" % s["addr"],
                      "size": s["size"]} for s in elf.sections
                     if s["flags"] & SHF_ALLOC and s["size"]],
    })
    return bytes(header) + bytes(image)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--elf", required=True)
    parser.add_argument("--nm", required=True, help="output of nm on the ELF")
    parser.add_argument("--imports", required=True, help="imports.inc path")
    parser.add_argument("--out", required=True)
    parser.add_argument("--report", help="JSON report path")
    parser.add_argument("--base", default="0x82000000")
    parser.add_argument("--page-size", default="0x10000")
    parser.add_argument("--stack-size", default="0x40000")
    parser.add_argument("--header-size", default="0x1000")
    args = parser.parse_args(argv)

    with open(args.elf, "rb") as handle:
        elf = ElfImage(handle.read())
    with open(args.nm, "r", encoding="ascii") as handle:
        nm_symbols = parse_nm(handle.read())
    with open(args.imports, "r", encoding="ascii") as handle:
        imports = parse_imports(handle.read())

    report = {}
    xex = build_xex(elf, nm_symbols, imports, int(args.base, 0),
                    int(args.page_size, 0), int(args.stack_size, 0),
                    int(args.header_size, 0), report)
    with open(args.out, "wb") as handle:
        handle.write(xex)
    report["output"] = args.out
    report["output_size"] = len(xex)
    if args.report:
        with open(args.report, "w", encoding="ascii") as handle:
            json.dump(report, handle, indent=2)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
