# The authored scene, built by the editor and drawn on the console

2026-09-17. The first image of this target produced by the editor's own build
graph rather than by hand, running the demodisc project's `cube_test` scene.

## What the editor did

Discovery, project script compilation, core transpilation, scene and material
cooking, code compilation, media layout and packaging all ran for platform
`x360`, ending in this repository's builder. It staged 36 cooked files, wrote
the scene catalog naming `cube_test` at `cooked/scenes/rendering/cube_test.hasset`,
and called `tools/build-image.ps1`, which compiled the generated core and
linked an image of 9,506,816 bytes carrying 8,287,126 bytes of content.

The generated core carried the project's own scripts. Eleven source files
survived pruning out of ninety-three, including `CubeTestSpinComponent` and
`DemoDiscOrbitCameraComponent`, and each was transpiled to C++ and compiled
into the image.

## What the console did

From `authored-scene.hel-log.txt`:

| Marker | Value |
|---|---|
| Authored scenes in the catalog | 1 |
| Cameras in the first draw | 3 |
| Drawables drawn / skipped | 6 / 0 |
| Draw calls the engine recorded | 6 |
| Frames presented | 60 |
| Failed heap allocations | 0 |

The scene is resolved by id through the engine's own scene manager, loaded
from the archive inside the image, and drawn through the Xenos command ring.

## The captures

`capture-0.png` and `capture-2.png` are 1.87 seconds apart. The cube has
turned 180 degrees about Y between them: the left and right faces change from
olive and teal to light blue and salmon, which are exactly the colours the
normals `(0,0,-1)`, `(-1,0,0)`, `(0,0,+1)` and `(+1,0,0)` map to under the
debug shading. Nothing in the backend animates anything, so that rotation is
the project's `CubeTestSpinComponent` running as transpiled C++ on the
console.

## Reproduced from one command

The steps above were first run separately while the failures were being found.
They were then run again as a single invocation of `tools/build-authored.ps1`,
which staged the project, pruned it, built the builder, published the codegen,
ran the editor headlessly and linked the image with no manual step. That image
boots to the same scene; `wrapper-capture.png` catches the cube face-on, on
the +X face, whose colour is what the normal `(1,0,0)` maps to. Its hash is
the second entry in `hashes.txt`.

## What this run does not show

- **Textures.** None are uploaded. The log names the first cooked texture the
  scene asked for and says so. The texture objects carry real dimensions and
  no pixels.
- **Material colour.** The cooked material's base colour is read and carried
  but not drawn with; the log reports it as `FFFFFFFF`. Faces are still shaded
  from their normals.
- **Two dimensional content.** The scene's instruction panel and text are
  packaged and reach the 2D manager, which counts them and draws nothing.
- **Hardware.** This is the pinned emulator. Nothing here has run on a
  console.
