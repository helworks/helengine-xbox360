# Shaders

Original Xenos microcode sources in the `tools/xenos_asm.py` text format
(documented at the top of that file). They are assembled at build time into
`build/shaders/<name>.bin` (big-endian dwords, the guest memory layout) and
`build/shaders/<name>.h` (a `static const uint32_t` array included by
`src/platform/xenia_guest/shader.c`), plus `<name>.lst` showing every
encoded instruction.

```powershell
./tools/build-shaders.ps1            # assembles shaders/*.xasm into build/shaders
python tests/xenos_asm_test.py       # field-level encoding tests
```

Provenance and licensing: the instruction encodings are facts about the GPU
taken from xenia-canary `f21ebd49e` (`src/xenia/gpu/ucode.h`, BSD-3-Clause),
which derives them from the MIT-licensed freedreno `instr-a2xx.h` and the
public IPR2015-00325 R400 documents. No Microsoft compiler, assembler,
header or sample was used; the shader sources and the assembler are original
work in this repository.

Binding contract (see `docs/xenia-backend-contract.md` section 6):

| Item | Value |
|---|---|
| Vertex fetch constant | `vf0`: `HelVertex` stream, stride 7 dwords, `k8in32` endian, size = vertices x 7 words |
| Vertex layout | dword 0-2 position `32_32_32_FLOAT` (w forced to 1 via `.xyz1`), dword 3-6 colour `32_32_32_32_FLOAT` |
| Vertex index | arrives in `r0.x` (hardware convention) |
| Constants | `c0..c3` = MVP columns, uploaded with `PM4_SET_CONSTANT` type 0 index 0 as 16 big-endian floats; `SQ_VS_CONST = 0x000FF000`, `SQ_PS_CONST = 0x000FF100` |
| Matrix convention | column-major storage, column vectors: `clip = c0*x + c1*y + c2*z + c3*w` |
| Exports | vertex: `oPos` (62), interpolator `o0` (0); pixel: `oC0` (0) |
| Register counts | `SQ_PROGRAM_CNTL.vs_num_reg = 3` (r0-r3), `ps_num_reg = 0` (r0), `vs_export_count = 0` (one interpolator) |
| Byte order | assembled dwords are stored big-endian in guest memory; the emulator swaps on load (`src/xenia/gpu/shader.cc:25-37`) |

Verification: launch with `--dump_shaders=<dir>`; the emulator writes each
loaded microcode blob and its own disassembly, which must match the source
intent (see `docs/evidence/`).
