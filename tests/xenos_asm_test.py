#!/usr/bin/env python3
"""Field-level checks of tools/xenos_asm.py against the layouts in
xenia-canary f21ebd49e src/xenia/gpu/ucode.h."""

import os
import pathlib
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import xenos_asm as xa  # noqa: E402


def bits(value, low, width):
    return (value >> low) & ((1 << width) - 1)


class AluEncoding(unittest.TestCase):
    def test_export_position_mad(self):
        code = xa.encode_alu("mad", xa.parse_dest("oPos"),
                             [xa.parse_src("c3"), xa.parse_src("r1.wwww"),
                              xa.parse_src("r3")])
        d0, d1, d2 = code
        self.assertEqual(bits(d0, 0, 6), 62)          # vector_dest = oPos
        self.assertEqual(bits(d0, 15, 1), 1)          # export_data
        self.assertEqual(bits(d0, 16, 4), 0xF)        # vector_write_mask
        self.assertEqual(bits(d0, 20, 4), 0)          # scalar_write_mask
        self.assertEqual(bits(d0, 26, 6), 50)         # retain_prev
        self.assertEqual(bits(d2, 24, 5), 11)         # mad
        self.assertEqual(bits(d2, 31, 1), 0)          # src1 constant
        self.assertEqual(bits(d2, 16, 8), 3)          # c3
        self.assertEqual(bits(d2, 30, 1), 1)          # src2 temp
        self.assertEqual(bits(d2, 8, 8), 1)           # r1
        self.assertEqual(bits(d2, 29, 1), 1)          # src3 temp
        self.assertEqual(bits(d2, 0, 8), 3)           # r3
        # r1.wwww: component-relative deltas (3-i)&3 -> 3,2,1,0
        swz2 = bits(d1, 8, 8)
        for i in range(4):
            delta = (swz2 >> (2 * i)) & 3
            self.assertEqual((i + delta) & 3, 3)
        self.assertEqual(bits(d1, 16, 8), 0)          # c3.xyzw identity

    def test_register_mask_and_negate(self):
        d0, d1, d2 = xa.encode_alu("mul", xa.parse_dest("r3.xy"),
                                   [xa.parse_src("-c0"), xa.parse_src("r1.xxxx")])
        self.assertEqual(bits(d0, 0, 6), 3)
        self.assertEqual(bits(d0, 15, 1), 0)
        self.assertEqual(bits(d0, 16, 4), 0b0011)
        self.assertEqual(bits(d1, 26, 1), 1)          # src1 negate
        self.assertEqual(bits(d2, 24, 5), 1)          # mul
        swz2 = bits(d1, 8, 8)
        for i in range(4):
            self.assertEqual((i + ((swz2 >> (2 * i)) & 3)) & 3, 0)

    def test_pixel_colour_export(self):
        d0, _d1, d2 = xa.encode_alu("max", xa.parse_dest("oC0"),
                                    [xa.parse_src("r0"), xa.parse_src("r0")])
        self.assertEqual(bits(d0, 0, 6), 0)
        self.assertEqual(bits(d0, 15, 1), 1)
        self.assertEqual(bits(d2, 24, 5), 2)


class FetchEncoding(unittest.TestCase):
    def test_uploaded_rgba8888_texture_uses_xenos_format_six(self):
        header = pathlib.Path(__file__).resolve().parents[1] / (
            "src/platform/xenia_guest/xenos_regs.h"
        )
        contents = header.read_text(encoding="utf-8")
        match = re.search(
            r"^#define XE_TEX_FORMAT_8_8_8_8 (\d+)u$",
            contents,
            re.MULTILINE,
        )
        self.assertIsNotNone(match, "RGBA8888 texture format constant is missing")
        self.assertEqual(int(match.group(1)), 6)

    def test_long_text_batch_counts_each_glyph_before_chunk_flush(self):
        source = pathlib.Path(__file__).resolve().parents[1] / (
            "src/platform/xenia_guest/X360RenderManager2D.cpp"
        )
        contents = source.read_text(encoding="utf-8")
        draw_text = contents.split(
            "void X360RenderManager2D::DrawText(", 1
        )[1].split("::RuntimeTexture *X360RenderManager2D::buildTexture(", 1)[0]
        self.assertRegex(
            draw_text,
            r"\+\+batchGlyphs;\s*\+\+textBatchGlyphs_;",
            "every glyph, including glyphs in long text runs, must be counted "
            "before the chunk flush",
        )

    def test_frame_ring_budget_stays_below_full_wrap_distance(self):
        source = pathlib.Path(__file__).resolve().parents[1] / (
            "src/platform/xenia_guest/video.c"
        )
        contents = source.read_text(encoding="utf-8")
        match = re.search(
            r"^#define HEL_MAX_FRAME_DWORDS \(([^\r\n]+)\)$",
            contents,
            re.MULTILINE,
        )
        self.assertIsNotNone(match, "frame ring budget definition is missing")
        self.assertEqual(
            match.group(1),
            "HEL_RING_DWORDS / 2u",
            "keep a half-ring per-frame bound so wrapping cannot advance the "
            "write pointer a full ring back to the same fence position",
        )

    def test_text_batches_use_32_glyphs_to_reduce_ring_draw_overhead(self):
        source = pathlib.Path(__file__).resolve().parents[1] / (
            "src/platform/xenia_guest/X360RenderManager2D.hpp"
        )
        contents = source.read_text(encoding="utf-8")
        match = re.search(
            r"kTextBatchGlyphLimit = (\d+);",
            contents,
        )
        self.assertIsNotNone(match, "text batch glyph limit is missing")
        self.assertEqual(
            int(match.group(1)),
            32,
            "32-glyph chunks reduce per-draw command-ring overhead while "
            "remaining inside the existing fenced-frame budget",
        )

    def test_full_fetch(self):
        reg, swz = xa.parse_fetch_dest("r1.xyz1")
        d0, d1, d2 = xa.encode_vfetch(reg, swz, 0, 0, 0, 7, 0, 57, False, False)
        self.assertEqual(bits(d0, 0, 5), 0)           # vertex fetch
        self.assertEqual(bits(d0, 5, 6), 0)           # r0
        self.assertEqual(bits(d0, 12, 6), 1)          # r1
        self.assertEqual(bits(d0, 19, 1), 1)          # must_be_one
        self.assertEqual(bits(d0, 20, 5), 0)
        self.assertEqual(bits(d0, 25, 2), 0)
        self.assertEqual(bits(d1, 0, 12), 0 | (1 << 3) | (2 << 6) | (5 << 9))
        self.assertEqual(bits(d1, 16, 6), 57)         # 32_32_32_FLOAT
        self.assertEqual(bits(d1, 30, 1), 0)          # not mini
        self.assertEqual(bits(d2, 0, 8), 7)           # stride
        self.assertEqual(bits(d2, 8, 23), 0)

    def test_mini_fetch_offset(self):
        reg, swz = xa.parse_fetch_dest("r2")
        _d0, d1, d2 = xa.encode_vfetch(reg, swz, 0, 0, 0, 0, 3, 38, True, False)
        self.assertEqual(bits(d1, 30, 1), 1)
        self.assertEqual(bits(d2, 8, 23), 3)
        self.assertEqual(bits(d1, 0, 12), 0 | (1 << 3) | (2 << 6) | (3 << 9))


class ControlFlow(unittest.TestCase):
    def test_pairs_and_addresses(self):
        source = """
        exec
          vfetch_full r1.xyz1, r0.x, vf0, stride=7, offset=0, format=32_32_32_FLOAT
          vfetch_mini r2, offset=3, format=32_32_32_32_FLOAT
        alloc position
        exec
          serialize mul r3, c0, r1.xxxx
          mad oPos, c3, r1.wwww, r3
        alloc interpolators
        exece
          max o0, r2, r2
        """
        dwords, _listing = xa.assemble(source)
        # 5 control-flow entries -> 3 pairs (9 dwords) + 5 instructions.
        self.assertEqual(len(dwords), 9 + 5 * 3)
        cf0_w0 = dwords[0]
        self.assertEqual(bits(cf0_w0, 0, 12), 3)      # first instruction slot
        self.assertEqual(bits(cf0_w0, 12, 3), 2)      # two instructions
        self.assertEqual(bits(cf0_w0, 16, 12), 0b0101)  # fetch, fetch
        cf0_w1 = dwords[1] & 0xFFFF
        self.assertEqual(bits(cf0_w1, 12, 4), 1)      # exec
        self.assertEqual(bits(cf0_w1, 9, 1), 1)       # PredicateClean
        cf1_w0 = (dwords[1] >> 16) | ((dwords[2] & 0xFFFF) << 16)
        cf1_w1 = dwords[2] >> 16
        self.assertEqual(bits(cf1_w1, 12, 4), 12)     # alloc
        self.assertEqual(bits(cf1_w1, 9, 2), 1)       # position
        self.assertEqual(cf1_w0, 0)
        cf2_w0 = dwords[3]
        self.assertEqual(bits(cf2_w0, 0, 12), 5)
        self.assertEqual(bits(cf2_w0, 16, 12), 0b0010)  # serialize on first
        cf4_w0 = dwords[6]
        cf4_w1 = dwords[7] & 0xFFFF
        self.assertEqual(bits(cf4_w1, 12, 4), 2)      # exece
        self.assertEqual(bits(cf4_w0, 0, 12), 7)
        cf5_w1 = dwords[8] >> 16
        self.assertEqual(bits(cf5_w1, 12, 4), 0)      # padding nop

    def test_texture_fetch_fields(self):
        """Pins every field of a texture fetch against the documented layout.

        The layout is TextureFetchInstruction::Data in the emulator's ucode.h.
        A wrong field here does not fail a build: it samples the wrong
        constant, or with the wrong filter, and shows up as a texture that is
        subtly or completely wrong on screen. That is why each field is
        checked rather than the three words compared against a recorded blob,
        which would say only that something changed.
        """
        dwords, _listing = xa.assemble(
            "exec\n tfetch r2.xyzw, r1.xy, tf3\nalloc colors\nexece\n"
            " max oC0, r2, r2\n")
        # Three control-flow entries plus a padding nop pack into two pairs,
        # which is six dwords, so the first instruction starts at dword 6.
        word0, word1, word2 = dwords[6], dwords[7], dwords[8]
        self.assertEqual(bits(word0, 0, 5), 1)      # opcode: texture fetch
        self.assertEqual(bits(word0, 5, 6), 1)      # source register r1
        self.assertEqual(bits(word0, 11, 1), 0)     # no loop addressing
        self.assertEqual(bits(word0, 12, 6), 2)     # destination register r2
        self.assertEqual(bits(word0, 18, 1), 0)     # no loop addressing
        self.assertEqual(bits(word0, 19, 1), 1)     # fetch_valid_only
        self.assertEqual(bits(word0, 20, 5), 3)     # fetch constant tf3
        self.assertEqual(bits(word0, 25, 1), 0)     # coordinates normalized
        # The source swizzle is three components of two bits. The caller named
        # two, so the third takes z rather than whatever the register holds.
        self.assertEqual(bits(word0, 26, 6), 0 | (1 << 2) | (2 << 4))
        # The destination swizzle is four components of three bits.
        self.assertEqual(bits(word1, 0, 12), 0 | (1 << 3) | (2 << 6) | (3 << 9))
        for shift in (12, 14, 16, 24, 26):          # every texture filter
            self.assertEqual(bits(word1, shift, 2), 3)  # defer to the constant
        self.assertEqual(bits(word1, 18, 3), 7)     # anisotropic: defer
        self.assertEqual(bits(word1, 21, 3), 7)     # arbitrary: defer
        self.assertEqual(bits(word1, 28, 1), 1)     # use_comp_lod
        self.assertEqual(bits(word1, 29, 1), 0)     # use_reg_lod
        self.assertEqual(bits(word1, 31, 1), 0)     # not predicated
        self.assertEqual(bits(word2, 0, 1), 0)      # no register gradients
        self.assertEqual(bits(word2, 2, 7), 0)      # no level-of-detail bias
        self.assertEqual(bits(word2, 14, 2), 1)     # two-dimensional
        self.assertEqual(bits(word2, 16, 5), 0)     # no texel offset in x
        self.assertEqual(bits(word2, 21, 5), 0)     # no texel offset in y

    def test_rejects_out_of_range_texture_constant(self):
        with self.assertRaises(xa.AsmError):
            xa.assemble("exec\n tfetch r0, r0.xy, tf32\nalloc colors\n"
                        "exece\n max oC0, r0, r0\n")

    def test_rejects_missing_exece(self):
        with self.assertRaises(xa.AsmError):
            xa.assemble("exec\n mul r0, r0, r0\n")


if __name__ == "__main__":
    unittest.main()
