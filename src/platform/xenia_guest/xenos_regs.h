/*
 * Xenos GPU register indices, packet encodings and field layouts used by the
 * guest backend. Values are facts about the hardware as modelled by the
 * pinned xenia-canary revision (src/xenia/gpu/register_table.inc, xenos.h,
 * registers.h); see docs/xenia-backend-contract.md for the source lines.
 */
#ifndef HEL_XENOS_REGS_H
#define HEL_XENOS_REGS_H

#include <stdint.h>

/* Guest virtual base of the GPU register block; index = (addr & 0xFFFF) / 4. */
#define XE_GPU_MMIO_BASE 0x7FC80000u
#define XE_GPU_REG_CP_RB_WPTR 0x01C5u

#define XE_GPU_REG_RB_SURFACE_INFO 0x2000u
#define XE_GPU_REG_RB_COLOR_INFO 0x2001u
#define XE_GPU_REG_RB_DEPTH_INFO 0x2002u
#define XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL 0x200Eu
#define XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR 0x200Fu
#define XE_GPU_REG_PA_SC_WINDOW_OFFSET 0x2080u
#define XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL 0x2081u
#define XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR 0x2082u
#define XE_GPU_REG_VGT_MAX_VTX_INDX 0x2100u
#define XE_GPU_REG_VGT_MIN_VTX_INDX 0x2101u
#define XE_GPU_REG_VGT_INDX_OFFSET 0x2102u
#define XE_GPU_REG_RB_COLOR_MASK 0x2104u
#define XE_GPU_REG_PA_CL_VPORT_XSCALE 0x210Fu
#define XE_GPU_REG_SQ_PROGRAM_CNTL 0x2180u
#define XE_GPU_REG_SQ_CONTEXT_MISC 0x2181u
#define XE_GPU_REG_RB_DEPTHCONTROL 0x2200u
#define XE_GPU_REG_RB_BLENDCONTROL0 0x2201u
#define XE_GPU_REG_RB_COLORCONTROL 0x2202u
#define XE_GPU_REG_PA_CL_CLIP_CNTL 0x2204u
#define XE_GPU_REG_PA_SU_SC_MODE_CNTL 0x2205u
#define XE_GPU_REG_PA_CL_VTE_CNTL 0x2206u
#define XE_GPU_REG_RB_MODECONTROL 0x2208u
#define XE_GPU_REG_VGT_OUTPUT_PATH_CNTL 0x2284u
#define XE_GPU_REG_PA_SC_VIZ_QUERY 0x2293u
#define XE_GPU_REG_PA_SU_VTX_CNTL 0x2302u
#define XE_GPU_REG_SQ_VS_CONST 0x2307u
#define XE_GPU_REG_SQ_PS_CONST 0x2308u
#define XE_GPU_REG_RB_COPY_CONTROL 0x2318u
#define XE_GPU_REG_RB_COPY_DEST_BASE 0x2319u
#define XE_GPU_REG_RB_COPY_DEST_PITCH 0x231Au
#define XE_GPU_REG_RB_COPY_DEST_INFO 0x231Bu
#define XE_GPU_REG_RB_DEPTH_CLEAR 0x231Du
#define XE_GPU_REG_RB_COLOR_CLEAR 0x231Eu
#define XE_GPU_REG_RB_COLOR_CLEAR_LO 0x231Fu
#define XE_GPU_REG_SHADER_CONSTANT_000_X 0x4000u
#define XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 0x4800u

/* RB_MODECONTROL.edram_mode */
#define XE_EDRAM_MODE_COLOR_DEPTH 4u
#define XE_EDRAM_MODE_COPY 6u

/* RB_COPY_CONTROL bits */
#define XE_COPY_COLOR_CLEAR_ENABLE (1u << 8)
#define XE_COPY_DEPTH_CLEAR_ENABLE (1u << 9)

/* Formats */
#define XE_COLOR_RT_FORMAT_8_8_8_8 0u
#define XE_DEPTH_RT_FORMAT_D24S8 0u
#define XE_TEXTURE_FORMAT_8_8_8_8 6u
#define XE_ENDIAN_NONE 0u
#define XE_ENDIAN_8IN16 1u
#define XE_ENDIAN_8IN32 2u

/* VGT_DRAW_INITIATOR */
#define XE_PRIM_RECTANGLE_LIST 8u
#define XE_PRIM_TRIANGLE_LIST 4u
#define XE_SOURCE_SELECT_DMA 0u
#define XE_SOURCE_SELECT_AUTO_INDEX 2u
#define XE_DRAW_INITIATOR(prim, source, num_indices) \
    ((prim) | ((source) << 6) | ((num_indices) << 16))

/* PM4 type-3 opcodes */
#define PM4_NOP 0x10u
#define PM4_DRAW_INDX 0x22u
#define PM4_IM_LOAD_IMMEDIATE 0x2Bu
#define PM4_SET_CONSTANT 0x2Du
#define PM4_EVENT_WRITE_SHD 0x58u
#define PM4_XE_SWAP 0x64u

/* Packet headers (count = number of following dwords, at least 1). */
#define XE_PACKET_TYPE0(index, count) \
    ((((uint32_t)(count) - 1u) << 16) | ((uint32_t)(index) & 0x7FFFu))
#define XE_PACKET_TYPE2() 0x80000000u
#define XE_PACKET_TYPE3(opcode, count) \
    (0xC0000000u | (((uint32_t)(count) - 1u) << 16) | ((uint32_t)(opcode) << 8))

/* VGT_EVENT_INITIATOR values for EVENT_WRITE_SHD */
#define XE_EVENT_PS_DONE_TS 3u

/* EDRAM geometry: 80x16 samples per 32-bit tile, 2048 tiles. */
#define XE_EDRAM_TILE_WIDTH 80u
#define XE_EDRAM_TILE_HEIGHT 16u
#define XE_EDRAM_TILE_COUNT 2048u

#endif
