# Xbox 360 text rendering design

## Goal

Render engine `ITextDrawable2D` content on the Xbox 360 backend using the font
atlas and glyph metrics that are already present in cooked `FontAsset` data.
The authored demo's interface text should become visible in Xenia, with layout
and text effects matching the engine's existing 2D renderer.

The scope is the X360 backend's text draw path. It does not introduce a new
font format, font cooker, glyph cache, or shaping system.

## Existing contracts

The generated core exposes `ITextDrawable2D` with text, font, scale, size,
alignment, wrapping, tint, shadow, outline, and parent position. `FontAsset`
already owns a runtime atlas texture, atlas dimensions, line height, space
width, and a `Dictionary<char, FontChar>`. Each `FontChar` supplies a
normalized atlas source rectangle, vertical offset, and advance width.

Font deserialization calls the platform `RenderManager2D` to create the atlas
texture. The X360 backend therefore receives the font texture through the
same `X360RuntimeTexture` path that sprites use. The existing textured UI draw
path already samples that texture, applies vertex tint, blends the batch, and
copies submitted geometry into the frame arena.

The engine's reference renderers establish the layout behavior: positions are
rounded to whole pixels; glyph width and height come from the normalized
source rectangle multiplied by atlas dimensions and font scale; line height
comes from `FontAsset.LineHeight`; spaces advance by `FontInfo.SpaceWidth`;
newlines begin a new line; wrapping uses the text box width divided by font
scale; and each line uses the selected left, center, or right alignment.
Shadow is submitted first, then the four outline offsets, then the main tint.
Glyph bearing fields are not applied by the reference renderers.

## Rendering design

`X360RenderManager2D::DrawText` will validate the drawable, parent, font,
character map, and X360 atlas handle. Invalid or unavailable font data skips
the run and is reported once, following the backend's existing diagnostic
style.

For a valid run, the renderer will use the generated core's text wrapping and
line-offset helpers so behavior stays aligned with the current engine. It
will walk the resulting text in order, advancing spaces, changing line origin
on newlines, skipping carriage returns and unsupported glyphs, and producing
one textured quad for each glyph and effect pass. UVs come directly from
`FontChar.SourceRect`; destination bounds use the reference layout equations.

Glyph quads will be submitted through the existing textured UI batch function
in chunks of at most 16 visible glyphs. Each glyph can emit at most six quads
(shadow, four outline sides, and fill), so a full chunk contains at most 384
vertices and 576 indices, within the backend limits of 4096 vertices and
65535 indices. The renderer will reuse bounded scratch storage between
submissions. It will not reorder glyphs, effects, or text drawables; the
camera's existing ordered queue remains authoritative.

Text accounting will distinguish requested runs from runs and glyphs that
were actually submitted. A failed batch will be counted as skipped without
preventing later drawables from rendering. Existing sprite, rounded-rectangle,
and text paths will continue sharing the frame UI arena and command ring.

## Compatibility and limits

Glyph coverage follows the font asset's existing `Dictionary<char>`; this
change adds no Unicode shaping, kerning, or fallback-font behavior. Spaces,
line breaks, wrapping, alignment, tint, shadow, and outline use the same
semantics as the engine reference renderer. Rotation remains unused, as in the
current engine reference path.

The frame arena and command ring remain finite. Chunking bounds temporary
geometry and individual submissions; if a complete frame exceeds the existing
arena or ring budget, the backend's current failure reporting applies. This
work does not add dynamic arena growth or a separate text-specific ring.

## Verification

Build the authored-scene XEX against the matching generated core, content
archive, and scene catalog, then run it in the pinned Xenia build with a
capture. Confirm that text requests produce submitted glyphs, the scene's
labels are visible, frame presentation continues, and no text batch reports an
arena or ring failure. The capture should also show that text stays in front
of its intended panel background. Do not use the generic generated core or
probe catalog for this run; those inputs do not describe the authored scene.
