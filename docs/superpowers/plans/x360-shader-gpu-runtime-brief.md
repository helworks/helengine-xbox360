# Opt-in shader GPU diagnostic runtime/build bridge

Read this first. Implement a diagnostic only, not materials/runtime artifact loader. User requested Luna. No emulator launch, captures, desktop control, worktrees, commits or subagents. Use apply_patch, preserve concurrent edits. Host generator runs in a separate agent with a fixed header contract (below); don't edit its files.

## Owned paths

Modify only narrowly guarded hooks in src/platform/xenia_guest/shader.c, mesh.c, xenia_guest.h and src/demo/main.c. Add src/platform/xenia_guest/shader_gpu_probe.inc, src/demo/shader_gpu_probe.inc, tools/build-shader-gpu-probe.ps1, tests/shader_gpu_runtime_test.c and a focused test runner if needed. Do NOT modify tools/build.ps1/build-core.ps1/build-image.ps1/test-host.ps1, video.c or X360RenderManager2D.cpp: they have concurrent edits. No shared Engine/PS3 changes.

## Design

All diagnostic C code/hooks under #if defined(HEL_SHADER_GPU_PROBE) && HEL_SHADER_GPU_PROBE. No change to normal build logic. shader.c includes shader_gpu_probe.inc after existing helpers; generated header path defaults to ../../../build/shader-gpu-probe/probe_shaders.h (from xenia_guest directory), overridable HEL_SHADER_GPU_PROBE_HEADER for tests. New internal APIs: hel_shader_gpu_probe_set_active(int), hel_shader_gpu_probe_is_active(void), int hel_shader_gpu_probe_emit(void), void hel_shader_gpu_probe_constants(void). Default inactive. Set active/inactive invalidates legacy g_emitted_valid so A/probe/A always restores programs. Probe emit validates generated ucode, uploads BOTH programs, programs SQ fields from generated metadata, uses separate VS/PS banks exactly as legacy shader setup. Report ID and dword counts once. Constants helper uploads generated numeric IEEE words into VS physical c0 and PS physical c256 (PM4_SET_CONSTANT type0), no packet for zero count. Respect ring overflow; no raised budgets.

mesh.c::draw: when diagnostic active, emit generated programs instead of hel_shaders_emit_kind and generated constants instead of legacy mvp/model/light constants. All existing vertex/texture descriptor and draw emission stays reused. Only diagnostic draw activates this. Return errors and reset active even when drawing fails. Existing texture binder fixed tf1, vertexvf0; host validates layouts/collision before generating header.

main.c clear_frame: include new demo inc under macro and call its frame-overlay function after regular core scene draw and before present. Existing frame_begin/present/normal core scene+text remain. No hooks in cube mode needed; wrapper forces HEL_DEMO_MODE=0 (clear/core scene), HEL_CORE_BOOT=1. Probe overlay lazily initializes a tiny distinct RGBA2x2 texture, uses existing hel_draw_ui_textured with sane centered clip-space quad vertices/color/UV, identity transform argument (diagnostic constants actually come from header), and keeps alpha visible. A small second constant-UV patch can verify distinct UV lanes; don't obscure whole screen. Enable around exactly diagnostic draws then disable unconditionally. Log completed frame count via hel_frame_count every60frames (actual present count, not just submitted frames), with marker at600; do not stop/freeze after1frame. Existing video.c logs first-frame ring cost; don't edit it.

## Header contract from host worker

tools/shader-probe/gpu_probe.py CLI: --vertex HLSL --pixel HLSL --vertex-layout JSON --pixel-layout JSON --shaderc DLL --output-dir DIR. entry main, generates probe_shaders.h / probe_manifest.json + VS/PS evidence files.

Header include stdint.h; arrays uint32_t hel_probe_vs[], hel_probe_ps[], hel_probe_vs_constants[], hel_probe_ps_constants[]. Macros HEL_PROBE_VS_DWORDS, HEL_PROBE_PS_DWORDS, HEL_PROBE_VS_CONSTANT_DWORDS, HEL_PROBE_PS_CONSTANT_DWORDS, HEL_PROBE_VS_REGISTER_FIELD, HEL_PROBE_PS_REGISTER_FIELD, HEL_PROBE_INTERPOLATOR_FIELD (highestslot/count-minus-one), HEL_PROBE_UPLOAD_DWORDS, HEL_PROBE_ID string. Empty constant arrays sentinel0 but count0. No reinterpretation of file bytes.

## Build wrapper

New tools/build-shader-gpu-probe.ps1 uses config.local.json plus explicit Shaderc argument/env. Fixed output build/shader-gpu-probe (avoids arbitrary include path macro quoting). Calls generator with tests/shaders/fixtures/transform_vs.hlsl, texture_ps.hlsl, gpu_probe_vs_layout.json (host owns), texture_runtime_layout.json. Calls existing tools/build.ps1 with -OutDir build/shader-gpu-probe/image and -Define HEL_SHADER_GPU_PROBE=1,HEL_DEMO_MODE=0,HEL_CORE_BOOT=1. Expose CoreGenerated/CoreLib/ContentRoot/CatalogRoot passthrough so actual authored scene can be tested. No launch. Protect normal build/xenia executable. Build native cache MUST include generated header identity: generated files in output aren't in build.ps1 source fingerprint, so pass an additional -DHEL_SHADER_GPU_PROBE_BUILD_ID=<numeric/hex token derived from header SHA> OR isolate NativeObjectCacheRoot by FULL header SHA. Choose fullhash cache subdir under diagnostic output, so changed header cannot hit stale objects. No need edit concurrent cache implementation. Check every external exit code, make old diagnostic XEX unavailable on generation/build failure without touching normal image. Record XEX/header/program hashes and reproduction command in diagnostic output/docs.

## Verification

TDD on host C ring harness: include actual shader.c under macro with small controlled header overriding generated path; mock only ring/log primitives, assert actual emitted packets/words/register fields/stage constant banks; A/probe/A reloads legacy; errors rejected; inactive doesn't change legacy. Make test compile/run with native gcc in pinned Docker (host PATH has no gcc). Parent confirmed /usr/bin/gcc exists; Docker needs escalated access from parent. You may prepare tests and tell parent exact command if sandbox denies. Also compile normal and macro-enabled PPC target hooks via real build once host generator ready (parent does final build). Never claim GPU behavior on compile evidence.

Read existing test conventions and keep test doubles narrow. Report docs/superpowers/plans/x360-shader-gpu-runtime-report.md with changes, RED/GREEN, tests/commands and concerns. Return short status. No automatic commit.

## Source/graph evidence

Project C-dev-helworks-helengine-x360 generation2026-09-23T16:41:22Z; graph main.c clear_frame52..82, hel_demo_main84..174. Relevant path coverage metadata_changed; parent directly read main.c, shader.c, mesh.c draw263..400, texture.c115..153, xenia_guest.h, frame.h and build.ps1. mesh has vf0 stride12, position0/UV6/color8. texture binder tf1; tf0 overlaps vf0..2. Existing build.ps1 supports Define,OutDir,CoreLib,CoreGenerated,CatalogRoot and new concurrent NativeObjectCacheRoot. toolchain config pinned devkitPPC Docker. Parent handles toolchain access and final integration, host agent owns gpu_probe.py/fixtures/test. Ask about API uncertainty before broad edits.
