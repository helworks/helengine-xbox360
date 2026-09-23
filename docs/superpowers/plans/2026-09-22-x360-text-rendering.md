# Xbox 360 text rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task in the current checkout. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render engine text drawables on Xbox 360 with their cooked font atlas, engine-compatible layout, and configured text effects.

**Architecture:** Extend `X360RenderManager2D::DrawText` to consume the existing `FontAsset` atlas and glyph metrics, use the generated core's wrapping, line-offset, and effect-pass helpers, then submit ordered glyph quads through the existing textured UI path in bounded batches. Preserve camera render order and the current UI arena/command-ring lifetime.

**Tech Stack:** C++17 X360 guest backend, generated C++ engine core, existing `hel_draw_ui_textured` path, pinned Xenia Canary emulator.

**Spec:** `docs/superpowers/specs/2026-09-22-x360-text-rendering-design.md`

## Global Constraints

- Use no more than 32 visible glyphs per submitted text batch.
- Support at most six effect passes per glyph: shadow, four outline sides, and fill.
- A full text batch contains at most 768 vertices and 1152 indices.
- Stay within the backend limits of 4096 vertices, 65535 indices, and the 0x40000-byte UI arena.
- Preserve the engine's `Dictionary<char>` glyph coverage; add no font-format, cooker, Unicode-shaping, kerning, or fallback-font subsystem.
- Preserve the camera's ordered drawable submission; do not reorder text, glyphs, or effect passes.
- Coalesce only consecutive text drawables with the same atlas, flushing at 32 glyphs and before any sprite/shape or viewport change.
- Reuse the existing X360 texture, blending, UI arena, and command-ring paths.

## Review Focus

- Missing font, atlas, or X360 texture handle: skip the text run safely and report it once.
- Spaces, carriage returns, line breaks, and unsupported characters: advance or skip exactly as the reference renderer does.
- Wrapping and left/center/right alignment at fractional positions and narrow widths: use the generated engine helpers and pixel snapping rules.
- Simultaneous shadow and outline: preserve shadow, outline sides, then fill ordering for each glyph.
- Long text and effect-heavy chunks: flush at the 32-visible-glyph limit and retain frame presentation within arena and ring budgets.

---

### Task 1: Render text from the existing font atlas

**Files:**
- Modify: `src/platform/xenia_guest/X360RenderManager2D.hpp`
- Modify: `src/platform/xenia_guest/X360RenderManager2D.cpp`
- Modify: `src/demo/core_boot.cpp`

**Interfaces:**
- Consumes: `ITextDrawable2D` getters, `FontAsset::Texture`, `FontAsset::Characters`, `FontChar`, `TextLayoutUtils::WrapText`, `TextLineOffsets2D::Build/Resolve`, `TextRenderEffectPassBuilder::Build`, and the existing `submit(..., HelTexture)` method.
- Produces: `DrawText` batches of at most 32 visible glyphs, reusing renderer-owned arrays sized for 768 `HelVertex` values and 1152 indices; counters for requested text runs, submitted runs, skipped runs, and submitted glyphs.

- [x] Add the bounded text scratch arrays and counters to `X360RenderManager2D`. Keep the arrays on the renderer instance so the largest effect-heavy batch does not consume about 40 KiB of guest stack.
- [x] Include the generated headers for `FontAsset`, `FontChar`, `TextLayoutUtils`, `TextLineOffsets2D`, `TextRenderEffectPass`, and `TextRenderEffectPassBuilder`; retain the existing backend vertex and texture declarations.
- [x] Replace the `DrawText` stub with validation for null drawable/parent/font, missing character map, null atlas, non-X360 atlas, and zero handle. Count every request; skip invalid runs and log the missing-font/atlas condition once.
- [x] Copy the drawable text to a local `HeCppString`. Use the reference renderer's minimum scale of `0.0001`. When wrapping is enabled, call `TextLayoutUtils::WrapText` with `max(1, round(size.X / fontScale))`. Build line offsets with `TextLineOffsets2D::Build(text, font, finalString, scale, atlasWidth)`.
- [x] Build effect passes once with `TextRenderEffectPassBuilder::Build`. The generated list owns its pass objects; delete the list after the run, including when no visible glyphs are found.
- [x] Walk the string in order. Ignore `\r`; on `\n`, advance by `max(LineHeight * scale, 1)`, reset horizontal advance, and select the next line offset; for space, advance by `FontInfo.SpaceWidth * scale`; for an absent glyph, emit nothing.
- [x] For each present glyph, calculate pixel size from `SourceRect.Z/W * AtlasWidth/Height * scale`, snap the drawable origin and line offset as the reference renderer does, and emit one quad per effect pass using `glyph.OffsetY`, pass offset/color, and the glyph's normalized `SourceRect` UVs.
- [x] Coalesce consecutive same-atlas text drawables into the current batch, flushing at 32 visible glyphs, before a non-text drawable, before a camera viewport change, and at frame end. For each glyph quad use indices `{0,1,2,0,2,3}` offset to its four vertices. Increment submitted-run/glyph counters only for batches accepted by `submit`; count a run as skipped if no batch is accepted.
- [x] Update the first-frame and shutdown text diagnostics to report requested, submitted, and skipped runs plus glyphs submitted; remove the misleading `text runs not drawn` label while preserving existing first-draw reporting.
- [x] Review every early return for cleanup of the offsets array and owned effect-pass list. Keep the already-present rounded-rectangle and sprite diagnostics intact.

**Manual checks for this task:**

- Trace the invalid-font/atlas branches and confirm they return before dereferencing `FontAsset::Characters` or texture handles.
- Trace spaces, `\r`, `\n`, missing glyphs, and the 32-glyph flush boundary against the reference loop in `RenderCommandListBuilder2D.EmitText`.
- Confirm a maximum six-pass batch produces exactly 768 vertices and 1152 indices, both within `mesh.c` limits.

### Task 2: Run the authored scene and verify live presentation

**Files:**
- Modify: `docs/next-steps.md`
- Read: `tools/build.ps1`, `tools/run.ps1`, and the generated authored-scene inputs under `build/`

**Interfaces:**
- Consumes: Task 1's X360 text diagnostics and glyph submissions; authored generated core, content archive, and scene catalog.
- Produces: Xenia log evidence of submitted authored interface text and stable frame presentation; a roadmap update only if separately requested.

- [x] Build into a fresh output directory with the authored-scene inputs, not the generic core or probe catalog:

  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 `
    -Config tools/config.local.json `
    -OutDir build/x360-text-drawfps-running `
    -CoreGenerated build/from-editor/generated-core `
    -CoreLib build/resume-authored-core/libhelcore.a `
    -ContentRoot build/from-editor/content `
    -CatalogRoot build/resume-authored-catalog
  ```

- [x] Run the new image with a unique log name. Per the user's instruction, do not capture the screen or automate the emulator UI; launch Xenia with a non-activating visible window:

  ```powershell
  & 'C:\dev\helworks\emus\xenia_canary_windows\xenia_canary.exe' `
    --log_file=C:\dev\helworks\helengine-x360\build\logs\x360-text-drawfps-running-host.log `
    --log_level=2 --discord=false `
    --storage_root=C:\dev\helworks\helengine-x360\build\xenia-storage-drawfps-running `
    C:\dev\helworks\helengine-x360\build\x360-text-drawfps-running\helengine.xex
  ```

- [x] Read `build/logs/x360-text-drawfps-running-host.log` and confirm all 29 text runs submitted, 262 glyphs submitted, first present completed, and frame counters continued beyond the earlier fence-timeout point. The first frame used 0x1DD0 dwords, below the existing half-ring budget; no ring/fence or UI-arena error was logged.
- [x] User visually confirmed the authored text; the Draw FPS line was corrected by fixing long-run batch accounting and reducing draw overhead with 32-glyph chunks.
- [ ] Roadmap update remains out of scope; no capture was made, per the user's instruction.
- [ ] Leave the existing diagnostic edits in `X360RenderManager2D.cpp/.hpp` in place; do not stage or commit the user's pre-existing changes as part of this plan.

## Execution Notes

Use the current checkout; do not create, remove, or switch worktrees. Automated regression checks cover the RGBA texture format, long-text batch accounting, and ring/batch bounds. Runtime verification is the authored-scene build and Xenia log above; no capture is permitted. The final XEX presented the first frame, submitted all 29 requested text runs (262 glyphs), and continued frame counters beyond frame 2 with the existing half-ring budget. The visible Draw FPS line was verified in the host emulator; the corrected build uses 32-glyph batches.
