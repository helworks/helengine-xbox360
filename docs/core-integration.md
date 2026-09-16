# Compiling the engine core into the Xbox 360 image

Status: the transpiled engine core boots in the pinned Xenia build, and a
scene built with the engine's own types draws through the Xenos backend. A
camera and a cube entity are created in code, the cube's model is a
ModelAsset filled in code rather than cooked content, and the renderer draws
it by walking the engine's cameras and render queues. No cooked content is
packaged yet, and the 2D manager is still a stub.

## The pipeline

```powershell
./tools/generate-core.ps1                 # C# -> C++ for this target
./tools/build-core.ps1                    # C++ -> build/core/libhelcore.a
./tools/build.ps1 -OutDir build/xenia-core -Define HEL_DEMO_MODE=2
./tools/run.ps1 -Target build/xenia-core/helengine.xex -TimeoutSeconds 30
```

`generate-core.ps1` runs csharpcodegen's CLI against
`helengine/engine/helengine.core/helengine.core.csproj`. It uses the same
shape of invocation as the PS1 port's `scripts/generate-core-boot.ps1`, with
this target's differences: `--platform xbox360 --endianness big` and the two
provider headers below. It writes `build/generated-core/generated` and
refuses to reuse an existing directory unless `-Force` is passed, so a run
never mixes output from two generations. Nothing outside this repository is
written; the generator assembly is used where it is already built.

`build-core.ps1` compiles the generator's own unity translation unit, which
includes every generated source once. Per-file compilation does not link:
the generated code instantiates templates such as
`ContentManager::RegisterProcessor<T>` and `AssetContentProcessor_1<T>`
whose definitions live in other generated sources. The PS1 and Dreamcast
ports solve this the same way, with a finalized single translation unit.

## Capability profile

The generated config (`build/generated-core/generated/helcpp_config.hpp`)
records what this target asked for:

| Setting | Value | Why |
|---|---|---|
| endianness | big | the console is big-endian PowerPC |
| pointer size | 4 | 32-bit |
| std containers | all off | supplied through the provider header |
| `codegen-use-exceptions` | off | no unwinder is linked |
| `codegen-use-rtti` | **on** | the generated generic dispatch requires it; codegen reports CPP1001 without it, and the PS1 port keeps it on too |
| `codegen-use-std-math` | off | math comes from newlib through `X360Math.hpp` |
| hosted file system | off | there is no file system on this target |
| exception messages | full | a console image has no other way to say what the managed runtime objected to |
| forced-disabled features | `shaders`, `debug_overlay` | the backend uses raw Xenos microcode, not the engine's shader system |

## Two compile modes in one image

The guest's own code is compiled `-ffreestanding`, as before. The core and
anything that includes it are **not**, because the generated runtime
includes `<functional>` and `<memory>` unconditionally and libstdc++'s
headers refuse to compile in freestanding mode. Those translation units are
listed as `$coreSources` in `tools/build.ps1` and are compiled with hosted
headers and C++20, which is the standard the generator emits. The image is
still linked with `-nostdlib`.

That choice is what makes the libstdc++ containers usable, so the provider
header aliases them rather than reimplementing them:

- `src/platform/xenia_guest/X360RuntimeProvider.hpp` maps
  `he_cpp_custom::String/Vector/UnorderedMap/UnorderedSet/Hash/Function/SharedPtr`
  to the standard containers, and declares the two service hooks.
- `X360RuntimeProvider.cpp` implements `Fail` (report and stop; the core is
  generated with exceptions off, so this is where a C# `throw` lands) and
  `MonotonicMicroseconds` (the PowerPC time base). It also **defines the
  `std::__throw_*` helpers**. The library's own definitions throw, which
  would link the exception allocator, the terminate handler, the personality
  routine and the unwinder into an image built with `-fno-exceptions`.
- `X360Math.hpp/.cpp` expose newlib's math library and define `finite`,
  which newlib does not declare for this configuration.
- `X360LibcShim.cpp` supplies the C entry points libstdc++ and libsupc++
  still reference: `malloc`/`free`/`calloc`/`realloc` over the guest heap,
  `abort`, `atexit`, `getenv`, `strchr`, `strtoul`, and single-threaded
  `pthread_*`.

Small-data sections are worth one note: libsupc++'s type-info objects land
in `.sdata`, and `link.ld` gives them a home in the writable pages. They are
addressed absolutely, with no small-data-relative relocations surviving the
link, so r13 remains the KPCR the emulator hands us. The PS1 port needed a
synthetic `_gp` for the same objects; this target does not.

## C++ runtime in the guest

`heap.c` is a first-fit free list with coalescing over one physical block
(32 MiB by default, `-DHEL_HEAP_BYTES`). `cxx_runtime.cpp` routes
`operator new`/`delete` to it, defines `__cxa_pure_virtual` and
`__cxa_atexit`, and runs `.init_array` through `hel_cxx_init`. The heap is
created before the static constructors, because a constructor in the core
may allocate.

`src/demo/cxx_selftest.cpp` checks the environment before anything else
runs: static constructors, virtual dispatch, `new`/`delete` reaching the
guest heap, array destructors, function-local statics, 64-bit division
through libgcc. A failure stops the demo before a frame is submitted. One
detail worth keeping: GCC treats the replaceable `operator new` as
malloc-like and will move a counter read across an allocation, so the
measurements around `new` are fenced.

## What runs today

`src/demo/core_boot.cpp` constructs `CoreInitializationOptions`, a content
stream source, an empty `RuntimeSceneCatalog`, `PlatformInfo("xbox360",
"core-boot")` and the `Core` itself, then calls `Initialize` and, every
frame, `Update`, `Draw` and `CompleteFrameBoundary`. The guest's own cube
demo keeps drawing, so the screen shows the backend's output while the
managed loop runs.

Evidence from `build/logs/core-stability` (65 seconds, one launch):

| Measure | Value |
|---|---|
| frames presented | 107,580 |
| heap allocations, every report | 207 |
| heap bytes, every report | 21,424 |
| physical allocations | 5 |
| managed failures | none |

The heap counters are identical in all 1,793 sixty-frame reports, so the
managed frame neither leaks nor grows.

## Drawing the engine's scene

`X360RenderManager3D` implements the two things the engine needs from a
platform renderer:

- `BuildModelFromRaw` converts a `ModelAsset` into backend mesh storage.
  The Dreamcast port refuses this path and requires cooked assets; here it
  is the path that makes a content-free scene possible.
- `Draw` walks `ObjectManager`'s cameras, inverts each camera entity's world
  transform for the view matrix, builds the projection, then visits the
  camera's render queue and draws each drawable with its entity's world
  transform.

`src/demo/core_scene.cpp` builds the scene: an entity with a
`CameraComponent`, and an entity with a `MeshComponent` whose model is a cube
`ModelAsset` filled in code. It spins the cube through the entity's own
orientation, so the rotation on screen came from the engine's transform
rather than from the backend.

Two steps there are easy to miss and cost a crash or an empty queue: an
entity's component and child lists must be initialised before `AddComponent`
is legal, and a component registers itself with the object manager at the
moment it is added, so the entity must already be enabled by then.

The field of view comes from the camera. It used to be invented by each
renderer, which meant one authored scene framed differently on every
platform; it now lives on `CameraComponent`, defaulting to the 45 degrees
every renderer already used.

Evidence in `evidence/2026-09-16-engine-scene`: one camera, one drawable
drawn and none skipped, with the heap at 238 allocations and 23,696 bytes in
every report of the run. The captures show the cube spinning with faces
coloured from their normals. Two further captures hold the spin still and
change only the authored field of view, which is how the renderer is shown
to read it rather than choose its own:

| Authored field of view | Cube on screen | Predicted |
|---|---|---|
| 45 degrees (the default) | 347 x 347 px | 347.7 px |
| 80 degrees | 171 x 171 px | 171.0 px |

## Byte order

The generated `BinaryReader` and `BinaryWriter` used to assume the CPU
running the program is little-endian, which is wrong here and wrong
silently: every multi-byte value they read or wrote came out byte-swapped,
on this target only. It is fixed in csharpcodegen (`runtime/native_endian.hpp`), and
`src/demo/endian_test.cpp` checks it on this console before any frame is
submitted. A failure stops the demo.

One thing that header must not do, and briefly did: equate the platform's
declared endianness with the CPU's. `HE_CPP_PLATFORM_IS_LITTLE_ENDIAN` comes
from `PlatformSerializationEndianness` and describes cooked data, not the
processor. The Nintendo 64 is a big-endian target that declares
little-endian serialization, which is legitimate, so an assert tying the two
together would have failed that build for no reason. The host order now
comes only from the compiler.

Evidence is in `evidence/2026-09-16-core`, from two runs of the same check
against the same image with only the generated runtime differing:

| Run | `host is big-endian` | big-endian wire | little-endian wire |
|---|---|---|---|
| `endian-before-fix` | yes | wrote `04 03 02 01`, expected `01 02 03 04` | wrote `01 02 03 04`, expected `04 03 02 01` |
| `endian-after-fix` | yes | round trip correct | round trip correct |

## Provenance

The three fixes this target depends on are merged into the default branches
of their repositories:

| Repository | Merge | What it carries |
|---|---|---|
| csharpcodegen | `d8ad75d` on `master` | binary serialization converts between wire and host byte order |
| helengine | `c1caf35e` on `main` | the camera owns its field of view, and `Core.Draw` no longer dereferences a null 3D render manager |

`tools/generate-core.ps1` still reads from worktrees of those repositories
(`C:/dev/helworks/csharpcodegen-x360` and
`C:/dev/helworks/helengine-camera-fov`) rather than from the shared
checkouts, and both worktrees track the merged tips. That is not about
pinning unmerged work any more; it is because those shared checkouts are a
live build input for several ports, so whichever branch one of them happens
to sit on is what anyone building from it compiles. Reading from a worktree
means a regeneration here cannot be changed by, or disturb, another session.

## Known gaps

- **No content.** `EmptyContentStreamSource` answers every request with
  "not present" and logs the path. Cooked assets and a real scene catalog
  are the next piece, following the PS1 and Dreamcast packaging steps.
- **Render managers are stubs.** `Core::Draw` calls
  `RenderManager3D::Draw` without a null check
  (`Core::MeasureRenderManager3DDrawMilliseconds`), so the managers exist,
  but `BuildModelFromRaw`, `BuildTextureFromRaw` and the 2D draw calls only
  report. Wiring them to `mesh.c` and `video.c` is what turns the engine's
  own scene into pixels.
- **No input or audio backend.** `Initialize` receives null for both.
- **No cooked content.** The scene is built in code. Packaging cooked assets
  with a real scene catalog, as the PS1 and Dreamcast ports do, is the next
  step.
- **The 2D manager is still a stub.** Sprites and text report their calls
  and draw nothing.
- **No materials.** A drawable with an empty material array is legal in the
  engine, and the renderer colours vertices from their normals instead.
  Material colour needs a per-draw constant in the pixel shader.
- **No input or audio backend.** `Initialize` receives null for both.
- The image is 1.1 MiB packed, against 132 KiB before the core was linked
  in.
