# Helengine Xbox 360 v0.1: Xenia-first unlit cube

Date: 2026-09-15
Status: Accepted as the planning baseline on 2026-09-15; implementation started the same day (Milestone 0 complete, see ../../xenia-compatibility.md).

## Objective

Build a minimal engine executable that runs inside Xenia and renders a slowly rotating, unlit cube. Establish a reproducible open-toolchain path before expanding the engine. Validate on the available homebrew-capable Xbox 360 after the Xenia milestone.

## Confirmed constraints

- Xenia on PC is the first execution target.
- A homebrew-capable Xbox 360 is available for subsequent validation.
- No Microsoft XDK dependency, including compiler, headers, libraries, shader tools, or samples.
- Use openly licensed tools and record dependency provenance. Open-source copyleft build tools are acceptable; their license is distinct from the engine's license.
- Do not assume the first Xenia executable will also boot through XeLL unchanged.
- Runtime libraries and shader binaries need documented source and licensing; publicly downloadable code is not automatically a reusable dependency.

## Research basis

Read in full:

- ../../Xbox360_Development.md: toolchain/runtime separation, deployment environments, shader-generation risk, platform isolation, and eDRAM budget.
- ../../Xbox360_Rendering.md: color/depth residency, resolves, resource layout, frame submission, and hardware validation.

Additional source inspection:

- https://github.com/xenia-project/xenia/blob/master/src/xenia/cpu/elf_module.cc
- https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/user_module.cc

Xenia's inspected upstream code accepts executable 32-bit PowerPC ELF files and creates a guest module/thread execution environment. The ELF loader restricts segment virtual addresses to its supported range. This establishes a candidate executable route, not compatibility with arbitrary bare-metal startup or LibXenon graphics. Pin the actual Xenia revision and inspect that revision before implementing against it.

The research documents also discuss XDK-specific graphics facilities. Those are architectural references, not APIs assumed available in this project.

## Approach and alternatives

Recommended: first prove a minimal open-toolchain guest executable and presentation path in Xenia, then build the cube using the proven path. Keep emulator-specific startup and presentation isolated so a physical-console backend can follow.

Alternatives considered:

1. A PC-native renderer gives fast visual feedback but does not validate execution inside Xenia. Host tests remain useful for math and binary-data validation.
2. A LibXenon/XeLL-first backend prioritizes physical hardware but reverses the requested order and cannot be assumed to work unchanged in Xenia.
3. A complete custom SDK is much larger than this milestone. If missing runtime or GPU support requires such work, report that finding and redesign the milestone rather than hiding it inside cube rendering.

## Milestone 0: prove the open execution and graphics route

Deliver a short compatibility report and a minimal reproducible probe before committing to a renderer API.

- Record the installed Xenia variant, revision, configuration, PC graphics backend, and launch command.
- Inventory available open PowerPC compilers/linkers and choose one based on a successful build and launch. Do not select a language version or build environment before checking tool support.
- Inspect the pinned loader's ELF requirements, guest entry/thread calling convention, memory layout, and available kernel interfaces.
- Build a minimal guest executable; establish observable entry-point execution through a debugger, trace, or deterministic guest-memory result. A black window alone is not proof of execution.
- Determine how the guest initializes GPU submission, addresses GPU-visible memory, synchronizes work, and requests presentation under Xenia.
- Establish a reproducible route to the two required Xenos shaders: position transformation and unlit color output. Record source, generator/assembler, output format, and licensing. Do not assume desktop HLSL tools emit Xenos instructions.
- Inspect candidate dependencies at pinned revisions. Treat LibXenon as a candidate requiring compatibility and license review, not as an already selected runtime.

Exit condition: repeatable guest execution plus a documented graphics/shader route with concrete source references. The next milestone must prove that route on screen. If execution, GPU access, shader generation, or presentation is unsupported, record the precise blocker and the bounded work needed to resolve it before proceeding.

Stock Xenia is the baseline. Any emulator modification must be explicitly documented as a development dependency; a patched-emulator success is not evidence of stock-Xenia or console compatibility.

## Milestone 1: video and frame presentation

Initialize the selected graphics path, allocate the necessary rendering and display surfaces, and continuously clear and present. Change the clear color deterministically to distinguish live frames from an old image.

Provisional target: 1280x720, one 32-bit color target, one 32-bit depth target, no MSAA. Raw color/depth storage is 7,372,800 bytes, approximately 7.03 MiB. Confirm hardware layout, alignment, pitch, and eDRAM addresses before use. Budget display buffers separately in main memory. If the proven path requires a different initial mode, document that choice before changing the target.

Exit condition: stable changing output and repeated successful launches, with logs identifying initialization and first presentation.

## Milestone 2: colored triangle

Generate the minimal shader pair and upload a static triangle. Define vertex layout, channel order, byte order, GPU addresses, and buffer visibility rules explicitly. Establish viewport, rasterization, and shader constants with the smallest draw possible.

Exit condition: expected geometry and vertex colors on screen, reproducible from a clean build.

## Milestone 3: rotating unlit cube

- Embed 24 vertices and 36 indices, allowing an independent solid color per face.
- Use one indexed draw per frame, a fixed perspective camera, and time-based rotation.
- Enable depth testing/writes and back-face culling.
- Document handedness, winding, matrix storage/multiplication convention, and clip-space depth for the selected backend.
- Keep geometry and shader resources alive across frames; allocate them during initialization.
- Synchronize constant updates and resource ownership so the CPU does not overwrite data still consumed by the GPU.

Frame sequence: read time, update transform, clear color/depth, bind state and geometry, draw, resolve as required, present using the proven synchronization path.

Exit condition: centered rotating cube with six distinct unlit face colors, correct perspective, and correct occlusion throughout a full rotation.

## Minimal architecture

Proposed ownership boundaries; create files only as their milestones need them:

- src/demo/cube.*: mesh data, camera settings, rotation.
- src/engine/math.*: vectors and matrices, tested on the host.
- src/engine/frame.*: frame lifecycle and the small rendering contract used by the demo.
- src/platform/xenia_guest/: guest startup, diagnostics, timing, GPU integration and presentation.
- shaders/: original shader sources and reproducible generation instructions.
- tools/: build/run entry points and binary inspection helpers.
- tests/: host math and generated-data checks.
- docs/: compatibility results and build/run instructions.

The Xenia guest backend contains code executed by the emulated PowerPC CPU. It is not a native Windows renderer. Extract shared Xbox GPU code once real overlap with the hardware backend is demonstrated.

One rendering thread is sufficient. Use explicit initialization failure reporting and stop drawing after a failed prerequisite. Avoid per-frame heap allocation. Wait for outstanding GPU work before releasing resources where the selected runtime supports teardown.

## Milestone 4: verification and reproducibility

- Host tests cover known matrix transformations, projection depth endpoints, and cube index bounds.
- Inspect executable architecture, entry point, sections, and shader/buffer sizes.
- Rebuild the guest and shader artifacts from documented source and pinned dependencies.
- Record fixed-angle screenshots to check face colors, winding, depth, and aspect ratio.
- Complete five successive launches and a ten-minute rotating-cube run without corruption, hangs, or growing engine allocation counts.
- Log initialization stages, selected mode, resource sizes, frame count, and CPU frame timing. Distinguish submission timing from measured GPU completion time.
- Preserve Xenia revision/configuration and verification evidence with the result.

Completion means the cube works inside the pinned Xenia build. Emulator timing is not an Xbox 360 performance claim. A console 60 Hz target remains subject to hardware measurement.

## Follow-up: physical Xbox validation

Confirm the console's actual launch environment, then implement the required startup/runtime and presentation differences. Reuse the demo, math, and compatible rendering code. Check byte order, memory/cache visibility, synchronization, display behavior, and sustained frame pacing on the device. Track this as a separate acceptance milestone.

## Deferred scope

Lighting, textures, model loading, audio, networking, controller interaction, an editor, scene/ECS systems, a job system, a render graph, streaming, MSAA, and optimization beyond basic resource discipline.

## Next decision

Implementation plans are in ../plans/2026-09-15-00-xenia-compatibility.md and ../plans/2026-09-15-01-video-to-cube.md. Execution is explicitly on hold at the user's request. Once authorized, begin with Milestone 0; backend implementation details must follow the verified compiler/runtime/shader route.
