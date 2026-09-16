#!/usr/bin/env python3
"""Minimal Xenos (Xbox 360 GPU) microcode assembler.

Encodes the small subset of the shader ISA the unlit cube needs: exec /
exece / alloc control flow, vertex fetch (full and mini), and vector ALU
operations (add, mul, max, min, mad, dp3, dp4) with register, constant and
export destinations. Field layouts follow xenia-canary f21ebd49e
src/xenia/gpu/ucode.h (BSD-licensed emulator source, itself derived from the
public freedreno instr-a2xx.h and the IPR2015-00325 R400 documents); the
encoding is verified by the emulator's own disassembly of the loaded shader
(run with --dump_shaders). No Microsoft tooling is involved.

Source format (one statement per line, `#` or `;` starts a comment):

    exec                      start a control-flow exec block
    exece                     start the final exec block (ends the shader)
    alloc position | alloc interpolators | alloc colors
    [serialize] vfetch_full DEST, SRC, vfN, stride=S, offset=O, format=F [, rounded]
    [serialize] vfetch_mini DEST, offset=O, format=F
    [serialize] OP DEST, SRC1[, SRC2[, SRC3]]      OP in add mul max min mad dp3 dp4

    DEST: rN[.mask] | oPos[.mask] | oN[.mask] | oCN[.mask]   (mask from xyzw)
          fetch DEST swizzle uses x y z w 0 1 _ per component (e.g. r1.xyz1)
    SRC:  [-]rN[.swz] | [-]cN[.swz]   swz = four of x y z w (default xyzw)
    F:    32_FLOAT 32_32_FLOAT 32_32_32_FLOAT 32_32_32_32_FLOAT 8_8_8_8 16_16_16_16_FLOAT

Instructions are numbered from the first slot after the control-flow pairs;
exec addresses and counts are computed automatically (max 6 per exec).
Output dwords are host-order values; the guest stores them big-endian and the
emulator swaps them back (src/xenia/gpu/shader.cc:25-37).
"""

import argparse
import json
import re
import struct
import sys

CF_NOP, CF_EXEC, CF_EXEC_END, CF_ALLOC = 0, 1, 2, 12
ALLOC_POSITION, ALLOC_INTERPOLATORS, ALLOC_COLORS = 1, 2, 2

VERTEX_FORMATS = {
    "8_8_8_8": 6, "16_16_16_16_FLOAT": 32, "32_FLOAT": 36,
    "32_32_FLOAT": 37, "32_32_32_32_FLOAT": 38, "32_32_32_FLOAT": 57,
}
VECTOR_OPS = {"add": (0, 2), "mul": (1, 2), "max": (2, 2), "min": (3, 2),
              "mad": (11, 3), "dp4": (15, 2), "dp3": (16, 2)}
SCALAR_RETAIN_PREV = 50
EXPORT_POSITION = 62
COMPONENT_INDEX = {"x": 0, "y": 1, "z": 2, "w": 3}
FETCH_DEST_COMPONENT = {"x": 0, "y": 1, "z": 2, "w": 3, "0": 4, "1": 5, "_": 7}


class AsmError(Exception):
    pass


def parse_dest(text):
    """Returns (kind, index, mask_bits) for r#/oPos/o#/oC# destinations."""
    m = re.fullmatch(r"(oPos|oC(\d+)|o(\d+)|r(\d+))(?:\.([xyzw]{1,4}))?", text)
    if not m:
        raise AsmError("bad destination %r" % text)
    mask = 0xF
    if m.group(5):
        mask = 0
        for c in m.group(5):
            mask |= 1 << COMPONENT_INDEX[c]
    if m.group(1) == "oPos":
        return ("export", EXPORT_POSITION, mask)
    if m.group(2) is not None:
        return ("export", int(m.group(2)), mask)   # oC# pixel colour
    if m.group(3) is not None:
        return ("export", int(m.group(3)), mask)   # o# interpolator
    return ("reg", int(m.group(4)), mask)


def parse_fetch_dest(text):
    m = re.fullmatch(r"r(\d+)(?:\.([xyzw01_]{4}))?", text)
    if not m:
        raise AsmError("bad fetch destination %r" % text)
    swz = m.group(2) or "xyzw"
    value = 0
    for i, c in enumerate(swz):
        value |= FETCH_DEST_COMPONENT[c] << (3 * i)
    return int(m.group(1)), value


def parse_src(text):
    """Returns (is_temp, index, negate, swizzle_field) for an ALU source."""
    m = re.fullmatch(r"(-?)([rc])(\d+)(?:\.([xyzw]{4}))?", text)
    if not m:
        raise AsmError("bad source %r" % text)
    swz = m.group(4) or "xyzw"
    field = 0
    for i, c in enumerate(swz):
        # Component-relative: source = (i + delta) & 3, see
        # AluInstruction::GetSwizzledComponentIndex.
        delta = (COMPONENT_INDEX[c] - i) & 3
        field |= delta << (2 * i)
    return m.group(2) == "r", int(m.group(3)), m.group(1) == "-", field


def encode_alu(op_name, dest, srcs, predicated=False):
    opcode, operand_count = VECTOR_OPS[op_name]
    if len(srcs) != operand_count:
        raise AsmError("%s takes %d sources" % (op_name, operand_count))
    kind, index, mask = dest
    is_export = kind == "export"
    d0 = (index & 0x3F) | (0 << 6) | (0 << 7) | (0 << 8) | (0 << 14) | \
        (int(is_export) << 15) | (mask << 16) | (0 << 20) | (0 << 24) | \
        (0 << 25) | (SCALAR_RETAIN_PREV << 26)
    swiz = [0, 0, 0]
    neg = [0, 0, 0]
    reg = [0, 0, 0]
    sel = [1, 1, 1]  # unused sources default to temporary r0
    for i, src in enumerate(srcs):
        is_temp, sindex, negate, field = src
        swiz[i] = field
        neg[i] = int(negate)
        reg[i] = sindex & 0xFF
        sel[i] = int(is_temp)
    d1 = swiz[2] | (swiz[1] << 8) | (swiz[0] << 16) | (neg[2] << 24) | \
        (neg[1] << 25) | (neg[0] << 26) | (0 << 27) | (int(predicated) << 28)
    d2 = reg[2] | (reg[1] << 8) | (reg[0] << 16) | (opcode << 24) | \
        (sel[2] << 29) | (sel[1] << 30) | (sel[0] << 31)
    return [d0 & 0xFFFFFFFF, d1 & 0xFFFFFFFF, d2 & 0xFFFFFFFF]


def encode_vfetch(dest_reg, dest_swz, src_reg, src_component, fetch_constant,
                  stride, offset, fmt, mini, rounded):
    if not 0 <= fetch_constant < 96:
        raise AsmError("vertex fetch constant out of range")
    if not 0 <= stride <= 255:
        raise AsmError("stride out of range")
    if not -(1 << 22) <= offset < (1 << 22):
        raise AsmError("offset out of range")
    d0 = 0 | (src_reg << 5) | (0 << 11) | (dest_reg << 12) | (0 << 18) | \
        (1 << 19) | ((fetch_constant // 3) << 20) | \
        ((fetch_constant % 3) << 25) | (0 << 27) | (src_component << 30)
    d1 = dest_swz | (0 << 12) | (0 << 13) | (0 << 14) | (int(rounded) << 15) | \
        (fmt << 16) | (0 << 24) | (int(mini) << 30)
    d2 = stride | ((offset & 0x7FFFFF) << 8)
    return [d0 & 0xFFFFFFFF, d1 & 0xFFFFFFFF, d2 & 0xFFFFFFFF]


def cf_exec(opcode, address, count, sequence):
    if not 1 <= count <= 6:
        raise AsmError("exec blocks hold 1 to 6 instructions")
    if address >= 1 << 12:
        raise AsmError("exec address out of range")
    word0 = address | (count << 12) | (0 << 15) | (sequence << 16)
    word1 = (1 << 9) | (0 << 11) | (opcode << 12)  # PredicateClean=true
    return word0, word1


def cf_alloc(alloc_type, size=0):
    return size & 7, (alloc_type << 9) | (CF_ALLOC << 12)


def cf_nop():
    return 0, 0


def pack_cf_pairs(cfs):
    if len(cfs) % 2:
        cfs = cfs + [cf_nop()]
    dwords = []
    for i in range(0, len(cfs), 2):
        (a0, a1), (b0, b1) = cfs[i], cfs[i + 1]
        dwords += [a0 & 0xFFFFFFFF,
                   ((a1 & 0xFFFF) | ((b0 & 0xFFFF) << 16)) & 0xFFFFFFFF,
                   ((b0 >> 16) | ((b1 & 0xFFFF) << 16)) & 0xFFFFFFFF]
    return dwords


def split_args(text):
    return [a.strip() for a in text.split(",") if a.strip()]


def parse_keyword(args, key, default=None, required=False):
    for a in args:
        if a.startswith(key + "="):
            return a[len(key) + 1:]
    if required:
        raise AsmError("missing %s=" % key)
    return default


def assemble(source):
    """Returns (dwords, listing_lines)."""
    blocks = []      # each: {"cf": ("exec"|"exece"|"alloc", data), "instrs": [...]}
    listing = []
    for lineno, raw in enumerate(source.splitlines(), 1):
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        try:
            parts = line.split(None, 1)
            head = parts[0]
            rest = parts[1] if len(parts) > 1 else ""
            if head in ("exec", "exece"):
                blocks.append({"cf": (head, None), "instrs": []})
            elif head == "alloc":
                kind = rest.strip()
                alloc_type = {"position": ALLOC_POSITION,
                              "interpolators": ALLOC_INTERPOLATORS,
                              "colors": ALLOC_COLORS}[kind]
                blocks.append({"cf": ("alloc", (alloc_type, kind)), "instrs": []})
            else:
                serialize = False
                if head == "serialize":
                    serialize = True
                    parts = rest.split(None, 1)
                    head = parts[0]
                    rest = parts[1] if len(parts) > 1 else ""
                if not blocks or blocks[-1]["cf"][0] not in ("exec", "exece"):
                    raise AsmError("instruction outside an exec block")
                args = split_args(rest)
                if head == "vfetch_full":
                    dest_reg, dest_swz = parse_fetch_dest(args[0])
                    m = re.fullmatch(r"r(\d+)\.([xyzw])", args[1])
                    if not m:
                        raise AsmError("vfetch source must be r#.component")
                    vf = re.fullmatch(r"vf(\d+)", args[2])
                    if not vf:
                        raise AsmError("vfetch constant must be vf#")
                    fmt = VERTEX_FORMATS[parse_keyword(args, "format", required=True)]
                    code = encode_vfetch(
                        dest_reg, dest_swz, int(m.group(1)),
                        COMPONENT_INDEX[m.group(2)], int(vf.group(1)),
                        int(parse_keyword(args, "stride", required=True), 0),
                        int(parse_keyword(args, "offset", "0"), 0), fmt, False,
                        "rounded" in args)
                    blocks[-1]["instrs"].append(("fetch", serialize, code, line))
                elif head == "vfetch_mini":
                    dest_reg, dest_swz = parse_fetch_dest(args[0])
                    fmt = VERTEX_FORMATS[parse_keyword(args, "format", required=True)]
                    code = encode_vfetch(
                        dest_reg, dest_swz, 0, 0, 0, 0,
                        int(parse_keyword(args, "offset", "0"), 0), fmt, True,
                        False)
                    blocks[-1]["instrs"].append(("fetch", serialize, code, line))
                elif head in VECTOR_OPS:
                    dest = parse_dest(args[0])
                    srcs = [parse_src(a) for a in args[1:]]
                    code = encode_alu(head, dest, srcs)
                    blocks[-1]["instrs"].append(("alu", serialize, code, line))
                else:
                    raise AsmError("unknown statement %r" % head)
        except (AsmError, KeyError, IndexError, ValueError) as error:
            raise AsmError("line %d: %s (%s)" % (lineno, error, raw.strip()))

    if not blocks or blocks[-1]["cf"][0] != "exece":
        raise AsmError("the last control-flow entry must be exece")
    for block in blocks:
        if block["cf"][0] in ("exec", "exece") and not block["instrs"]:
            raise AsmError("empty exec block")

    cf_count = len(blocks)
    pair_count = (cf_count + 1) // 2
    address = pair_count  # instruction slots follow the control-flow pairs
    cfs = []
    instr_dwords = []
    for block in blocks:
        kind, data = block["cf"]
        if kind == "alloc":
            cfs.append(cf_alloc(data[0]))
            listing.append("cf alloc %s" % data[1])
            continue
        sequence = 0
        for i, (itype, serialize, _code, _line) in enumerate(block["instrs"]):
            bits = (1 if itype == "fetch" else 0) | (2 if serialize else 0)
            sequence |= bits << (2 * i)
        opcode = CF_EXEC_END if kind == "exece" else CF_EXEC
        cfs.append(cf_exec(opcode, address, len(block["instrs"]), sequence))
        listing.append("cf %s address=%d count=%d sequence=0x%03X" % (
            kind, address, len(block["instrs"]), sequence))
        for itype, serialize, code, line in block["instrs"]:
            listing.append("  /* %3d */ %s -> %08X %08X %08X" % (address, line, *code))
            instr_dwords += code
            address += 1
    dwords = pack_cf_pairs(cfs) + instr_dwords
    listing.insert(0, "control-flow entries: %d (%d pairs), instructions: %d, "
                   "dwords: %d" % (cf_count, pair_count, address - pair_count,
                                   len(dwords)))
    return dwords, listing


def write_outputs(dwords, listing, args):
    if args.out:
        with open(args.out, "wb") as handle:
            handle.write(struct.pack(">%dI" % len(dwords), *dwords))
    if args.header:
        symbol = args.symbol or "shader"
        lines = ["/* Generated by tools/xenos_asm.py from %s; do not edit. */" %
                 args.source,
                 "#include <stdint.h>",
                 "#define %s_DWORDS %du" % (symbol.upper(), len(dwords)),
                 "static const uint32_t %s[%d] = {" % (symbol, len(dwords))]
        for i in range(0, len(dwords), 3):
            lines.append("    " + ", ".join("0x%08Xu" % d for d in dwords[i:i + 3]) + ",")
        lines.append("};")
        with open(args.header, "w", encoding="ascii") as handle:
            handle.write("\n".join(lines) + "\n")
    if args.listing:
        with open(args.listing, "w", encoding="ascii") as handle:
            handle.write("\n".join(listing) + "\n")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source")
    parser.add_argument("--out", help="big-endian binary output")
    parser.add_argument("--header", help="C header with a uint32_t array")
    parser.add_argument("--symbol", help="array name for the header")
    parser.add_argument("--listing", help="human-readable encoding listing")
    args = parser.parse_args(argv)
    with open(args.source, "r", encoding="ascii") as handle:
        source = handle.read()
    try:
        dwords, listing = assemble(source)
    except AsmError as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    write_outputs(dwords, listing, args)
    print(json.dumps({"source": args.source, "dwords": len(dwords),
                      "sha256_input": None}))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
