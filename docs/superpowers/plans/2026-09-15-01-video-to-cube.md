# Video to Unlit Cube Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to execute task-by-task after implementation is authorized. Delegation remains subject to the user's multi-agent opt-in rules. All steps below are future work.

**Goal:** Render a stable rotating, six-colored unlit cube inside the pinned Xenia build, then prepare a separate hardware validation step.

**Architecture:** A single-threaded demo uses small math/frame modules and a guest platform backend. Resources are created during initialization and retained across frames; GPU ownership and presentation stay inside the backend.

**Tech Stack:** The compiler/runtime and native shader generation route verified by the compatibility plan; C-compatible module boundaries; PowerShell build/run wrappers; host tests for math and data. Select the source language standard after toolchain verification.

**Spec:** [Accepted design](../specs/2026-09-15-xenia-unlit-cube-design.md).

## Global constraints

- Xenia on PC is the first execution target.
- A homebrew-capable Xbox 360 is available for subsequent validation.
- No Microsoft XDK dependency, including compiler, headers, libraries, shader tools, or samples.
- Use openly licensed tools and record dependency provenance. Open-source copyleft build tools are acceptable; their license is distinct from the engine's license.
- Do not assume the first Xenia executable will also boot through XeLL unchanged.
- Runtime libraries and shader binaries need documented source and licensing; publicly downloadable code is not automatically a reusable dependency.
- Execution is on hold at the user's request.

## Prerequisite and precision boundary

[Compatibility plan](2026-09-15-00-xenia-compatibility.md) must produce `docs/xenia-backend-contract.md` with status `ready-for-clear`. Its final task must specialize this plan with verified backend calls and build commands before backend implementation begins. The task order, project-owned interfaces, and acceptance tests below are specified now; GPU packets, runtime calls, linker addresses, and shader instructions are deliberately not invented here.

## Project-owned interfaces

These are proposed internal interfaces, not claims about an existing SDK. Use them consistently when specializing the plan:

```c
#include <stdint.h>
typedef struct { float m[16]; } HelMat4;
typedef struct { float x, y, z, r, g, b, a; } HelVertex;
typedef uint32_t HelMesh;
typedef struct { uint32_t width, height; } HelVideoConfig;

/* Zero means success. An error stops drawing and identifies its stage. */
int hel_platform_init(void);
uint64_t hel_ticks(void);
uint64_t hel_tick_frequency(void);
void hel_log(const char *message);
int hel_video_init(const HelVideoConfig *config);
int hel_frame_begin(float r, float g, float b, float a);
int hel_mesh_create(const HelVertex *v, uint32_t nv,
                    const uint16_t *i, uint32_t ni, HelMesh *out);
int hel_draw_unlit(HelMesh mesh, const HelMat4 *mvp);
int hel_frame_present(void);
void hel_video_shutdown(void);

HelMat4 hel_identity(void);
HelMat4 hel_mul(HelMat4 a, HelMat4 b);
HelMat4 hel_translate(float x, float y, float z);
HelMat4 hel_rotate_y(float radians);
HelMat4 hel_perspective(float fovy, float aspect, float near_z, float far_z);
```

`hel_mesh_create` copies/uploads caller data into backend-owned persistent storage. `hel_frame_begin` waits for reusable per-frame storage and clears depth as well as color. `hel_draw_unlit` copies constants into storage protected until GPU completion. `hel_frame_present` submits/resolves/presents and advances ownership. `hel_video_shutdown` drains work before releasing resources and is safe after partial initialization.

## Task 1: Clear and present continuously

**Create:** `src/platform/xenia_guest/start.S`, `src/platform/xenia_guest/link.ld`, `src/platform/xenia_guest/platform.c`, `src/platform/xenia_guest/video.c`, `src/engine/frame.h`, `src/engine/frame.c`, `src/demo/main.c`, `tools/build.ps1`, `tools/run.ps1`, `docs/build-and-run.md`.

**Consumes:** Verified startup, graphics/presentation contract, and the probe's build/run process.

**Produces:** Implementations of platform/video/timing/frame interfaces and `build/xenia/helengine.elf` (or the explicitly verified alternative format).

- [ ] Reuse the proven startup without adding unverified runtime dependencies. Keep the original probe as a regression diagnostic.
- [ ] Implement platform timing with a monotonic source and documented frequency. Log startup, video initialization, selected mode, allocation sizes, and first presentation using the verified diagnostic channel.
- [ ] Allocate one color/depth rendering set and the display/command buffers required by the contract. For 1280x720, budget 7,372,800 raw color/depth bytes plus documented padding; main-memory display buffers are additional. Reject invalid or overlapping allocations before submission.
- [ ] Implement initialization failure paths in reverse resource-acquisition order. Never continue submitting after allocation, shader setup, or device initialization failure.
- [ ] Implement this diagnostic frame behavior using the verified backend:

```c
/* Alternate clear colors every 60 submitted frames. */
unsigned phase = (frame_number / 60u) & 1u;
int error = hel_frame_begin(phase ? 0.25f : 0.02f, 0.02f, 0.08f, 1.0f);
if (!error) error = hel_frame_present();
/* On error, log the failed stage and stop the frame loop. */
```

- [ ] Make build/run wrappers accept `-Config`; preserve checked process exit codes, output paths, and isolated emulator logs. Document exact commands in `docs/build-and-run.md`.
- [ ] Verify both clear colors appear repeatedly, correct aspect ratio, five successful launches, and an intentional invalid configuration producing a diagnostic with no GPU submission.
- [ ] Commit as `feat: initialize Xenia video and present clear frames`.

## Task 2: Draw an unlit triangle

**Create:** `src/platform/xenia_guest/mesh.c`, `src/platform/xenia_guest/shader.c`, `src/demo/triangle.c`, `shaders/README.md`, `tools/build-shaders.ps1`, plus shader source files in the verified assembler/compiler's native source format.

**Modify:** `src/platform/xenia_guest/video.c`, `src/engine/frame.h`, `src/demo/main.c`, `tools/build.ps1`.

**Consumes:** Task 1's frame lifecycle and compatibility contract's shader/resource layout.

**Produces:** `hel_mesh_create`, `hel_draw_unlit`, generated shader binaries, and a triangle diagnostic mode.

- [ ] Write original vertex-transform and color-output shader sources. Document binding slots, fetch descriptors, constant matrix convention, native instruction byte order, and reproducible generation command.
- [ ] Validate generated shader format, byte length, and alignment before creating the pipeline. Build fails on missing or incompatible shader output.
- [ ] Use the following initial triangle with identity transform:

```c
static const HelVertex triangle[] = {
    {-0.5f,-0.5f,0.5f, 1,0,0,1},
    { 0.5f,-0.5f,0.5f, 0,1,0,1},
    { 0.0f, 0.5f,0.5f, 0,0,1,1}
};
static const uint16_t triangle_indices[] = {0,1,2};
```

- [ ] Upload data once; use the verified endian/fetch conversion and GPU visibility protocol. Start with culling disabled so winding cannot hide the first triangle.
- [ ] Compare a captured frame with the expected vertex positions and interpolated colors. Log one draw and three indices. Exercise a rejected out-of-range index in the host-side mesh validator before GPU upload.
- [ ] Commit as `feat: render an unlit colored triangle in Xenia`.

## Task 3: Math and indexed cube

**Create:** `src/engine/math.h`, `src/engine/math.c`, `src/demo/cube.h`, `src/demo/cube.c`, `tests/math_test.c`, `tests/cube_test.c`, `tools/test-host.ps1`.

**Modify:** `src/demo/main.c`, `src/platform/xenia_guest/shader.c`, `src/platform/xenia_guest/video.c`.

**Consumes:** Triangle pipeline and the project interfaces above.

**Produces:** Cube mesh, transform functions, deterministic capture mode, and rotating demo mode.

- [ ] Fix engine math conventions: column-major storage, column vectors, `MVP = P * V * M`, right-handed view space looking along negative Z, and projected depth in [0,1]. Adapt at the backend boundary if the verified shader/fetch convention differs.
- [ ] Write failing host tests for identity, matrix order, Y rotation, and near/far projection. Use tolerance 0.00001 for ordinary float comparisons. Core projection expectations are:

```text
P = perspective(pi/3, 16/9, 0.1, 100)
project(P, (0,0,-0.1,1)).z / w == 0 within tolerance
project(P, (0,0,-100,1)).z / w == 1 within tolerance
identity * translation(1,2,3) == translation(1,2,3)
rotation_y(pi/2) * (1,0,0,1) == (0,0,-1,1) within tolerance
```

- [ ] Implement the five matrix operations and run those host tests. Projection matrix nonzero elements for this convention are:

```c
/* m[] is column-major; initialize every other element to zero. */
m[0] = f / aspect; m[5] = f; /* f = 1 / tan(fovy / 2) */
m[10] = far_z / (near_z - far_z);
m[11] = -1.0f;
m[14] = (far_z * near_z) / (near_z - far_z);
```

- [ ] Generate 24 vertices at cube extents +/-0.5, four per face, and 36 indices. Assign red/cyan/green/magenta/blue/yellow to +X/-X/+Y/-Y/+Z/-Z. Use counterclockwise triangles viewed from outside each face. Test index bounds, six indices per face, equal color across each face, and outward normals from triangle cross products.
- [ ] Position the camera at (0,0,3) looking toward the origin; use view translation (0,0,-3), vertical FOV pi/3, near 0.1, far 100. Rotate Y at 0.5 radians/second using elapsed monotonic time, wrapping the angle to avoid long-run precision loss.
- [ ] Enable depth writes, depth clear 1, and less-than depth testing. Configure back-face culling with any backend viewport winding inversion accounted for.
- [ ] Add a fixed-angle diagnostic configuration for 0, pi/4, pi/2, and pi radians. Compare face visibility and aspect ratio; preserve the triangle mode for diagnosis.
- [ ] Confirm one draw, 36 indices, and persistent mesh/shader allocations in the default cube mode. Commit as `feat: render a depth-tested rotating unlit cube`.

## Task 4: Stability and reproducible handoff

**Create:** `docs/v0.1-validation.md`.

**Modify:** `tools/build.ps1`, `tools/test-host.ps1`, `docs/build-and-run.md`, backend diagnostics, and `third_party/manifest.toml`.

**Consumes:** Working cube and all earlier verification evidence.

**Produces:** Reproducible build/run instructions and a completed acceptance record.

- [ ] Run host math/mesh tests, rebuild shaders and guest output, and inspect guest headers/symbols/resource sizes. Use a separate fresh output directory to verify a clean build without deleting arbitrary paths.
- [ ] Track owned allocations and bytes after initialization; count frames and record CPU submission timing separately from any supported GPU timing. Do not turn logging itself into a per-frame allocation source.
- [ ] Perform five successive launches and a ten-minute run. Capture start/end allocation counters and fixed-angle screenshots. Pass requires no growth in owned allocations, no corruption/hangs, and correct face/depth behavior.
- [ ] Confirm failure diagnostics identify the stage and that teardown does not release resources referenced by pending GPU work.
- [ ] Document tool and emulator revisions, executable/shader hashes, configuration, command lines, evidence location, and any limitations. Audit that all shipped/generated dependencies have recorded source and licensing.
- [ ] Commit as `docs: record reproducible cube validation`.

## Follow-up plan: hardware acceptance

**Create when authorized:** `docs/hardware-validation.md`, and `src/platform/x360_homebrew/` files selected after checking the actual console loader/runtime.

- [ ] Record the console's supported launch format and environment; do not infer XeLL support from the generic homebrew-capable description.
- [ ] Compare startup, GPU addressing/cache maintenance, timing, interrupts, synchronization, shader layout, and presentation with the Xenia contract.
- [ ] Implement only confirmed differences behind the existing interfaces; extract shared GPU code where the two implementations actually agree.
- [ ] Repeat clear, triangle, fixed-angle cube, five launches, and ten-minute stability checks on hardware. Measure console frame pacing separately from Xenia.
- [ ] Record hardware completion independently; Xenia v0.1 acceptance does not imply this follow-up has passed.

## Coverage and stopping point

The compatibility plan covers build/boot and tool provenance. Tasks 1-4 cover video, shaders, geometry, transforms, depth/culling, diagnostics, memory discipline, reproducibility, and stability. Hardware validation is explicitly subsequent. Textures, lighting, input, audio, networking, jobs, scene systems, and asset tooling remain outside v0.1.

Do not start any task until the user authorizes implementation. Do not execute downstream backend work until the compatibility contract has specialized its API and command details.
