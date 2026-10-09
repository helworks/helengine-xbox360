# GPU gate preparation (not executed)

These are source-verified constraints for the next Phase1 step, not a claim of GPU validation. Production renderer and legacy shaders have not been changed.

## Current boundaries

- `tools/build-shaders.ps1` assembles `shaders/*.xasm` into generated headers consumed by `shader.c`. It does not consume shader-probe JSON or HLSL.
- `shader.c::hel_shaders_emit_kind` selects static pairs using `HelShaderKind`, and hardcodes register/interpolator fields. A compiled probe cannot safely replace only a binary while leaving those fields unchanged.
- `shader.c::emit_shader` writes host integer dwords into `PM4_IM_LOAD_IMMEDIATE`. Probe `.bin` files are big-endian serialized dwords, so a reader must explicitly decode them; do not reinterpret file bytes as host-native integers. Generated C headers already contain numeric dwords.
- `texture.c::hel_texture_bind` writes texture fetch constant1. Its descriptor uses RGBA8, linear layout, clamp-to-edge, linear min/mag and base-map filtering, with no mip upload. It does not accept an arbitrary target slot or sampler state.
- Texture constant0 overlaps vertex fetch constants0..2. Existing mesh uses vf0. A future linked program allocation must reject collisions across VS/PS and vertex/texture descriptors; validating only texture-pair collisions is insufficient.
- Program upload cache is reset per frame and keyed by legacy enum. Probe program identity and generated metadata must be honored by any isolated GPU harness without breaking the legacy UI/text paths.

## Required isolated GPU probe

1. Build a separate, opt-in probe path; preserve legacy generated shader files and default renderer behavior. Do not enable the shared backend merely because CPU tests pass.
2. Use actual HLSL-produced transform/color/texture programs with explicit layouts; upload the generated constants and register/interpolator metadata, not the current hardcoded counts.
3. Draw a small known RGBA texture with distinct colors and alpha. Test vertex transforms, UV routing and authored arithmetic separately. Use texel centers first; later sample off-center to verify linear filtering independently of the CPU point-sampling oracle.
4. Record source/compiler/binary/XEX hashes, program dword counts, ring use, shader bindings and multi-frame progress in logs. A successful build or translator parse is not a rendered result.
5. Launch Xenia by CLI only with user authorization, without CUA, captures or forced foreground activation. User observes output. Preserve cube, text and FPS while checking sustained frames.

GateA stays open until real generated VS/PS execute correctly. Any failure must produce a diagnostic and a bounded fix, not fallback to a preselected legacy blob.

## Evidence

Source read on 2026-09-23: `tools/build-shaders.ps1`, `src/platform/xenia_guest/shader.c`, `src/platform/xenia_guest/texture.c:115`, `shaders/textured.ps.xasm`, `tools/xenos_asm.py:162`, and local Xenia `src/xenia/gpu/ucode.h:777`. codebase-memory was used first; metadata-changed coverage was followed by direct source reads. No console hardware validation was performed.
