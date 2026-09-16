# Xenia graphics backend contract

Date: 2026-09-15. Pinned emulator: xenia-canary `f21ebd49e979e44f081f474df78c3fbfee9cb3f2`
(see `xenia-compatibility.md`). Paths are relative to `src/xenia/` in that
revision. Everything below is derived from the emulator source; the parts not
yet exercised by a running guest are marked **hypothesis** and become facts
only when the rendering plan's milestones show them on screen.

**Classification: `ready-for-clear`.** Clearing and presenting needs only
kernel exports that the proven XEX2 import path reaches, ring-buffer packets,
and a resolve; no shader is involved. The triangle milestone additionally
needs a bounded component: a small Xenos microcode assembler (section 6).

## 1. Executable and runtime route (proven)

- Guest code is a freestanding C/assembly image built with devkitPPC GCC and
  wrapped by `tools/xexpack.py` into an XEX2 (proven by the probe, three runs).
- Kernel services are reached through XEX2 import thunks resolved by
  `XexModule::SetupLibraryImports`. Every export used must be listed in
  `imports.inc` with its ordinal from `kernel/xboxkrnl/xboxkrnl_table.inc`.
- Calling convention: r3-r10, then 32-bit stack slots at `r1+0x54+8*(n-8)`
  (`kernel/util/shim_utils.h:32-44,141-147`). `VdSwap` has ten arguments, so
  it needs an assembly wrapper that stores arguments 9 and 10 at `r1+0x54` and
  `r1+0x5C` of the wrapper's frame before the `bl`.
- Logging: `DbgPrint(format, ...)` (ordinal 3) writes `(DbgPrint) ...` to the
  emulator log. Timing: `mftb` returns `Clock::QueryGuestTickCount()`
  (`cpu/ppc/ppc_emit_control.cc:721-732`); its frequency is the host tick
  frequency by default (`base/clock.cc:39`), so the guest must read it with
  `KeQueryPerformanceFrequency` (ordinal 0x83, `kernel/xboxkrnl/xboxkrnl_threading.cc:456-461`)
  instead of assuming 50 MHz. `KeDelayExecutionThread` (0x5A) sleeps.

## 2. GPU-visible memory

| Caller | Signature | Source | Notes |
|---|---|---|---|
| guest | `uint32_t MmAllocatePhysicalMemoryEx(uint32_t flags, uint32_t size, uint32_t protect, uint32_t min_phys, uint32_t max_phys, uint32_t alignment)` ordinal 0xBA | `kernel/xboxkrnl/xboxkrnl_memory.cc:415-496` | `protect` must contain `X_PAGE_READWRITE` (4); `X_PAGE_WRITECOMBINE` (0x400) is accepted; `X_MEM_LARGE_PAGES` (0x20000000) selects 64 KiB pages and the `0xA0000000` alias, otherwise 4 KiB pages at `0xE0000000`. Returns a guest virtual address in that alias, or 0. Pass `min_phys=0`, `max_phys=0xFFFFFFFF` for no constraint. Allocations are top-down. |
| guest | `uint32_t MmGetPhysicalAddress(uint32_t virtual)` ordinal 0xBE | `xboxkrnl_memory.cc:643-655` | Physical address for ring-buffer and fetch-constant fields. |
| guest | `void MmFreePhysicalMemory(uint32_t type, uint32_t base)` ordinal 0xBD | `xboxkrnl_memory.cc:506-514` | Teardown. |

The CPU writes big-endian data; the GPU reads little-endian and applies the
per-resource endian swap: vertex fetch constants use `Endian::k8in32` (2) for
32-bit floats, index buffers use `VGT_DMA_SIZE.swap_mode = k8in16` (1) for
16-bit indices, and command-stream dwords are swapped by the ring reader
(`ReadAndSwap`), so packets are written big-endian as ordinary guest stores.
Physical memory written by the CPU after the D3D12 shared memory uploaded it
is tracked by write watches on allocated physical pages (`memory.cc:2029-2081`);
allocating through `MmAllocatePhysicalMemoryEx` keeps that tracking valid.

## 3. Command submission

| Step | Interface | Source |
|---|---|---|
| Create ring | `void VdInitializeRingBuffer(uint32_t physical_ptr, uint32_t size_log2)` ordinal 0x1C3. Ring size in bytes is `1 << (size_log2 + 3)`; the buffer is zeroed. | `xboxkrnl_video.cc:336-343`, `gpu/command_processor.cc:447-454` |
| Read-pointer writeback | `void VdEnableRingBufferRPtrWriteBack(uint32_t physical_ptr, uint32_t block_size_log2)` ordinal 0x1B6. After each processed block the emulator stores the read index (in dwords, big-endian) to that physical address. | `xboxkrnl_video.cc:345-351`, `command_processor.cc:372-381,456-465` |
| Kick | 32-bit store of the write index (dwords, wrapping at the ring size) to guest virtual `0x7FC80000 + 0x1C5*4 = 0x7FC80714` (`CP_RB_WPTR`). The MMIO handler forwards it to `UpdateWritePointer`; the GPU thread then executes `[read, write)`. | `gpu/graphics_system.cc:102-105,297-309`, `command_processor.cc:489-495`, `gpu/pm4_command_processor_implement.h:1417-1462` |
| Completion | `PM4_EVENT_WRITE_SHD` (type 3, opcode 0x58, 3 dwords: `initiator`, `physical address | endian`, `value`): writes `value` (swapped per the low two address bits) to memory when reached. The guest polls that dword to know all earlier packets were consumed (host-side execution order; the D3D12 work itself is asynchronous, see section 8). | `pm4_command_processor_implement.h:884-919` |

Packet encodings (`gpu/xenos.h:1688-1715`):

- Type 0: `(count-1) << 16 | register_index` followed by `count` dwords written
  to consecutive registers (`gpu/register_table.inc` indices).
- Type 2: `0x80000000` no-op.
- Type 3: `0xC0000000 | (count-1) << 16 | opcode << 8` followed by `count`
  dwords. `PM4_SET_CONSTANT` (0x2D): first dword `type << 16 | index`, type 0 =
  ALU float constants (register 0x4000 + index), 1 = fetch constants (0x4800 +
  index), 4 = context registers (0x2000 + index) (`command_processor.cc:738-775`).
- `PM4_IM_LOAD` (0x27): `physical_address | shader_type`, `size_dwords` loads a
  shader from memory; `PM4_IM_LOAD_IMMEDIATE` (0x2B): `shader_type`,
  `size_dwords`, then the microcode dwords inline (`pm4_command_processor_implement.h:1309-1369`).
- `PM4_DRAW_INDX` (0x22): `viz_query_condition`, `VGT_DRAW_INITIATOR`, then for
  `SourceSelect::kDMA` `VGT_DMA_BASE` (physical index address) and
  `VGT_DMA_SIZE` (`num_words | swap_mode << 30`) (`pm4_command_processor_implement.h:1049-1181`).

## 4. Presentation (Xenia-specific packet, kernel-issued on hardware)

`VdSwap` (ordinal 0x25B, `xboxkrnl_video.cc:466-551`) is the kernel operation
that presents on both the console and the emulator. In Xenia it writes into a
64-dword slot the guest reserved in the ring: a type-0 write of the six
front-buffer texture fetch dwords to `SHADER_CONSTANT_FETCH_00_0` (0x4800),
then `PM4_XE_SWAP` (type 3, opcode 0x64, 4 dwords: `'SWAP'`, front-buffer
physical address, width, height), then NOPs. The command processor's
`ExecutePacketType3_XE_SWAP` calls `IssueSwap`, and the D3D12 backend reads
the front buffer through texture fetch constant 0 (`gpu/d3d12/d3d12_texture_cache.cc:1239-1274`).
`PM4_XE_SWAP` is marked "Xenia only" in `gpu/xenos.h:1675`; on hardware the
same `VdSwap` call programs the display controller instead, so the engine must
call `VdSwap` rather than emit the packet itself.

Arguments (all required, ten of them):

| # | Register | Value |
|---|---|---|
| 1 | r3 | guest virtual address of the 64-dword ring slot (the guest advances its write index past it before calling) |
| 2 | r4 | pointer to the 6-dword front-buffer fetch constant (Direct3D texture header form: `base_address` field = front-buffer **virtual** address >> 12; Xenia converts it to physical) |
| 3 | r5 | writeback pointer (unused by Xenia; pass a valid dword) |
| 4 | r6 | system command buffer pointer (unused; pass a valid dword) |
| 5 | r7 | system command buffer GPU identifier (unused) |
| 6 | r8 | pointer to a dword holding the front-buffer virtual address (must equal fetch `base_address << 12`) |
| 7 | r9 | pointer to a dword holding the texture format: `k_8_8_8_8` (6) |
| 8 | r10 | pointer to a dword holding the color space: 0 |
| 9 | stack `r1+0x54` | pointer to a dword holding the width (must equal fetch `size_2d.width + 1`) |
| 10 | stack `r1+0x5C` | pointer to a dword holding the height |

Front-buffer fetch constant layout (`gpu/xenos.h:1235-1336`): dword0 `type=2 (kTexture)`,
`pitch = width_in_pixels >> 5` at bit 22, `tiled=1` at bit 31; dword1
`format=6 (k_8_8_8_8)`, `endianness=k8in32 (2)` at bit 6, `base_address` at
bit 12; dword2 `width-1 | (height-1) << 13`; dword3 swizzle `RGBA` (0x688 =
X,Y,Z,W in 3-bit fields) with point filters; dword5 `dimension = k2DOrStacked (1)`
at bit 9. The front buffer is the tiled resolve destination from section 5.

## 5. Initial render target and clear (no shader)

Provisional mode 1280x720, one `k_8_8_8_8` color target and one `kD24S8` depth
target, no MSAA:

| Register (index) | Value | Source |
|---|---|---|
| `RB_SURFACE_INFO` (0x2000) | `surface_pitch = 1280`, `msaa_samples = 0` | `gpu/registers.h:670-680` |
| `RB_COLOR_INFO` (0x2001) | `color_base = 0` tiles, `color_format = 0` | `registers.h:736-753` |
| `RB_DEPTH_INFO` (0x2002) | `depth_base = 720` tiles, `depth_format = 0` | `registers.h:835-849` |
| `RB_MODECONTROL` (0x2208) | `edram_mode = 4 (kColorDepth)` for draws, `6 (kCopy)` for resolves | `registers.h:660-668`, `xenos.h:928-951` |
| `PA_SC_WINDOW_SCISSOR_TL/BR` (0x2081/0x2082) | `0,0` / `1280,720` | `registers.h:622-645` |
| `PA_SC_SCREEN_SCISSOR_TL/BR` (0x200E/0x200F) | same | |
| `PA_SC_WINDOW_OFFSET` (0x2080) | 0 | |

EDRAM budget (`gpu/xenos.h:222-258`): a tile is 80x16 samples at 32 bpp
(5120 bytes). 1280x720 rounds to 16x45 = 720 tiles per 32-bit surface. Color
at tiles `[0, 720)`, depth at `[720, 1440)`; 1440 of 2048 tiles (7.03 MiB of
10 MiB) used, no wrap. The remaining 608 tiles are free.

Clear: the resolve draw can clear the EDRAM region it copies.
`RB_COPY_CONTROL` (0x2318) bits `color_clear_enable` (8) and
`depth_clear_enable` (9) with `RB_COLOR_CLEAR` (0x231E, packed 8_8_8_8) and
`RB_DEPTH_CLEAR` (0x231D) apply after the copy (`gpu/draw_util.cc:986-1330`
reads them into the resolve info). **Verified (Tasks 1-3):** frame =
resolve-with-clear (sets EDRAM color and depth to the clear values; its copy
output is discarded) then the draws, then a resolve without clear into the
front buffer, then `VdSwap`. The clear-through-resolve path works in the
emulator, so no full-screen clear draw was needed; `hel_frame_begin` keeps
it (`src/platform/xenia_guest/video.c`).

Resolve draw requirements (`draw_util.cc:986-1130`, `d3d12/d3d12_command_processor.cc:2609-2613`):

- `RB_MODECONTROL.edram_mode = kCopy`; the draw packet is a `PM4_DRAW_INDX`
  with `prim_type = kRectangleList (8)`, `source_select = kAutoIndex (2)`,
  `num_indices = 3`.
- Vertex fetch constant 0 (registers 0x4800/0x4801): `type = 3 (kVertex)`,
  `address = physical >> 2`, `endian = k8in32`, `size = 6` words pointing at
  three big-endian float pairs `(x0,y0) (x1,y0) (x0,y1)` in pixels; with
  `PA_SU_VTX_CNTL.pix_center = 0` the emulator adds 0.5, so pass corner
  coordinates minus 0.5 as Direct3D 9 did (`draw_util.cc:1012-1051`).
- `RB_COPY_CONTROL.copy_src_select = 0` (color 0), `copy_command = 0 (kRaw)`,
  `copy_sample_select = 0`.
- `RB_COPY_DEST_BASE` (0x2319) = front-buffer physical address (4 KiB aligned),
  `RB_COPY_DEST_PITCH` (0x231A) = `1280 | 720 << 16`, `RB_COPY_DEST_INFO`
  (0x231B) = `copy_dest_endian = k8in32 (2)`, `copy_dest_format = 6`,
  `copy_dest_number = 0`, `copy_dest_array = 0`. The destination is a tiled
  2D `k_8_8_8_8` texture; buffer size 1280x720x4 = 3,686,400 bytes.

## 6. Shaders (bounded extension, required for the triangle)

Xenos shaders are raw microcode: control-flow instruction pairs (3 dwords per
pair) followed by ALU/fetch instructions (3 dwords each), formats in
`gpu/ucode.h` (BSD-licensed emulator source, itself derived from the public
freedreno `instr-a2xx.h` and the IPR2015-00325 sequencer documents). Xenia
parses them in `gpu/shader_translator.cc:42-200`. There is no open assembler
on this machine, and desktop HLSL tools do not emit this format, so the
rendering plan includes `tools/xenos_asm.py`: a Python encoder for the six
instruction kinds the cube needs (exec/exece/alloc control flow, vertex fetch,
`mul`/`mad`/`dp4` ALU ops, and export to `oPos`/`o0`/`oC0`). Estimated size:
about 200 lines plus tests against the field layouts in `ucode.h`.
Verification: run with `--dump_shaders=<dir>` so the emulator writes each
loaded microcode blob and its own disassembly; the disassembly must match the
intended program before the draw is trusted.

Binding facts for the unlit pipeline (**verified** by the triangle and the
cube, `evidence/2026-09-15-triangle` and `evidence/2026-09-15-cube`; the
emulator's own disassembly of the loaded microcode matches the sources):

- Vertex fetch: `vfetch_full` from fetch constant 0, stride in dwords,
  `k_32_32_32_FLOAT` position then `k_32_32_32_32_FLOAT` color at dword offset 3
  (`ucode.h:690-775`); the vertex index arrives in r0.x.
- Constants: `PM4_SET_CONSTANT` type 0 at index 0 loads `c0..c3` (the MVP
  columns) into `SHADER_CONSTANT_000_X` (0x4000+). `SQ_VS_CONST` (0x2307) and
  `SQ_PS_CONST` (0x2308) select the constant window (`registers.h:256-281`).
- `SQ_PROGRAM_CNTL` (0x2180): `vs_num_reg`/`ps_num_reg` = registers-1,
  `vs_export_count` = interpolators-1, `vs_export_mode = 0`.
- Exports: vertex position `62`, interpolator `0`; pixel color `0`
  (`ucode.h:1792-1851`).
- Draw state: `PA_CL_VTE_CNTL` (0x2206) = 0x43F: all six enables plus
  `vtx_w0_fmt` (bit 10, "W is not 1/W"; without it the emulator takes the
  reciprocal of the exported W, `d3d12_command_processor.cc:4116-4126`, and
  any vertex with W != 1 is clipped away), with `PA_CL_VPORT_*`
  (0x210F-0x2114) = `xscale 640, xoffset 640, yscale -360, yoffset 360,
  zscale 1, zoffset 0`; `PA_CL_CLIP_CNTL` (0x2204) = `dx_clip_space_def`
  (bit 19: clip on, Direct3D 0..w depth); `PA_SU_SC_MODE_CNTL` (0x2205)
  culling bits 0-1 and `face` bit 2; `RB_DEPTHCONTROL` (0x2200)
  `z_enable|z_write_enable|zfunc`;
  `RB_COLOR_MASK` (0x2104) = 0xF; `RB_BLENDCONTROL0` (0x2201) = 0x00010001
  (`src = one, dst = zero` for color and alpha, i.e. copy);
  `VGT_MAX_VTX_INDX` (0x2100) = 0xFFFFFF, `VGT_MIN_VTX_INDX` (0x2101) = 0,
  `VGT_INDX_OFFSET` (0x2102) = 0, `VGT_OUTPUT_PATH_CNTL` (0x2284) = 0.
- Winding and depth (cube evidence): with `PA_SU_SC_MODE_CNTL = 0x2`
  (`cull_back`, `face = 0`) and the cube's faces wound counter-clockwise as
  seen from outside, the outside faces survive, so counter-clockwise is the
  front face under this viewport (`yscale < 0`). `RB_DEPTHCONTROL = 0x16`
  (`z_enable`, `z_write_enable`, `zfunc = less`) with the resolve depth
  clear of 1.0 orders faces correctly: the `HEL_CULL_MODE=0` build (no
  culling) renders pixel-identical to the culled build at pi/4
  (`cube-a1-nocull-capture-0.png` vs `cube-a1-capture-0.png`).

## 7. Synchronization and ownership

- The ring is written only by the guest thread and read only by the GPU
  thread; the guest must not overwrite ring entries between its last kick and
  the read index reported by write-back (or a completion fence).
- Per-frame constants go into the ring (`PM4_SET_CONSTANT`), so they never
  alias memory the GPU still reads. Geometry and shaders live in physical
  allocations made once at initialization and are never rewritten.
- Frame pacing: `vsync = true` limits `PM4_XE_SWAP` presentation to the frame
  limiter; the guest should wait for the previous frame's fence before
  reusing its swap slot.
- Teardown: wait for the last fence, then `MmFreePhysicalMemory`.

## 8. Hardware portability assumptions

Kernel exports, ring/register/packet encodings, EDRAM layout and resolve
registers are modelled on the console's own driver behaviour and are expected
to transfer. Emulator-only aspects: `PM4_XE_SWAP` is generated by the
emulator's `VdSwap`; fences complete when the emulator's command thread reaches
them, not when the host GPU finishes; MMIO writes other than `CP_RB_WPTR` are
ignored; `mftb` frequency equals the host tick frequency. The real console
also enforces XEX signing and a different `VdSwap` implementation; those are
the follow-up hardware milestone's concern.

## 9. Status and next experiment

Rendering plan Tasks 1-3 exercised every section above in the pinned
emulator: clear/present (`evidence/2026-09-15-clear`), unlit triangle
(`evidence/2026-09-15-triangle`) and the depth-tested rotating cube
(`evidence/2026-09-15-cube`: fixed angles 0, pi/4, pi/2, pi show blue,
cyan+blue, cyan and yellow faces; the no-cull build is pixel-identical).
Deviations from the first draft of this contract: ELF loading is blocked
(section 1), `PA_CL_VTE_CNTL` needs `vtx_w0_fmt` (section 6) and
`RB_BLENDCONTROL0` is written as one/zero rather than 0. Next: Task 4
stability and reproducibility (five launches, ten-minute run, allocation
counters), then the hardware follow-up in the rendering plan.
