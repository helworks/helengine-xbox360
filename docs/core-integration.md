# Compiling the engine core into the Xbox 360 image

Status: the transpiled engine core boots in the pinned Xenia build. It is
constructed, initialized and stepped once per frame with its own render
managers attached. Nothing is drawn through it yet, because no cooked
content is packaged into the image and the render managers do not reach the
Xenos backend.

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

## Byte order

The generated `BinaryReader` and `BinaryWriter` used to assume the CPU
running the program is little-endian, which is wrong here and wrong
silently: every multi-byte value they read or wrote came out byte-swapped,
on this target only. It is fixed in csharpcodegen
(`runtime/native_endian.hpp`, branch `fix/big-endian-binary-serialization`),
and `src/demo/endian_test.cpp` checks it on this console before any frame is
submitted. A failure stops the demo.

Evidence is in `evidence/2026-09-16-core`, from two runs of the same check
against the same image with only the generated runtime differing:

| Run | `host is big-endian` | big-endian wire | little-endian wire |
|---|---|---|---|
| `endian-before-fix` | yes | wrote `04 03 02 01`, expected `01 02 03 04` | wrote `01 02 03 04`, expected `04 03 02 01` |
| `endian-after-fix` | yes | round trip correct | round trip correct |

## Provenance

The generated core currently comes from two branches, not from the default
branches of those repositories:

| Repository | Branch | Commit | Why |
|---|---|---|---|
| csharpcodegen | `fix/big-endian-binary-serialization` | 8ae4e55 | the byte-order fix above |
| helengine | `fix/core-draw-without-render-managers` | c0157e99 | `Core.Draw` no longer dereferences a null 3D render manager |

Regenerating from the default branches would reintroduce both problems.

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
- **Render managers are stubs** (see above): that is the one gap between
  here and drawing the engine's own scene.
- The image is 1.1 MiB packed, against 132 KiB before the core was linked
  in.
