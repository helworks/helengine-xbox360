# Build and run (Xenia guest)

Prerequisites: Docker with the `devkitpro/devkitppc` image, Python 3.11,
Windows PowerShell, the pinned Xenia Canary build, and
`tools/config.local.json` copied from `tools/config.example.json` with local
paths. Pinned revisions and hashes are in `xenia-compatibility.md`.

The engine core is transpiled from C# and linked into the image; see
`core-integration.md`. `tools/build.ps1` needs its archive, so a fresh
checkout runs the first two commands once:

```powershell
# C# core -> C++ (build/generated-core) -> PowerPC archive (build/core)
./tools/generate-core.ps1 -Config tools/config.local.json
./tools/build-core.ps1 -Config tools/config.local.json

# Engine image -> build/xenia/helengine.{elf,xex,map,dis,nm,readelf.txt,xex.json,hashes.txt}
./tools/build.ps1 -Config tools/config.local.json

# Demo modes: 0 clear (default), 1 triangle, 2 rotating cube; fixed cube angles 0..3
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-triangle -Define HEL_DEMO_MODE=1
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-cube -Define HEL_DEMO_MODE=2
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-cube-a2 -Define HEL_DEMO_MODE=2,HEL_DEMO_FIXED_ANGLE=2
# Culling override (PA_SU_SC_MODE_CNTL bits): 0 none, 1 cull front, 2 cull back (default)
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-cube-a1-nocull -Define HEL_DEMO_MODE=2,HEL_DEMO_FIXED_ANGLE=1,HEL_CULL_MODE=0

# Shaders only (also run by build.ps1): shaders/*.xasm -> build/shaders
./tools/build-shaders.ps1 -Config tools/config.local.json

# Host tests: engine math, cube geometry, assembler encodings
./tools/test-host.ps1 -Config tools/config.local.json

# Failure-path build (unsupported video mode requested by the demo)
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-invalid -Define HEL_DEMO_INVALID_CONFIG=1

# With cooked content staged into the image (empty archive when absent)
./tools/build.ps1 -Config tools/config.local.json -ContentRoot build/content-staging

# Without the transpiled core (backend and demo only)
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-nocore -Define HEL_CORE_BOOT=0

# Teardown check: stop after 120 frames, shut video down, log allocations left
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-cube-limit -Define HEL_DEMO_MODE=2,HEL_DEMO_FRAME_LIMIT=120

# Launch for 30 s, log to build/logs/<run>/xenia.log, evaluate markers
./tools/run.ps1 -Config tools/config.local.json -TimeoutSeconds 30 -RunName clear-1
./tools/run.ps1 -Config tools/config.local.json -Target build/xenia-invalid/helengine.xex -ExpectFailure

# Compatibility probe (unchanged regression diagnostic)
./tools/build-probe.ps1; ./tools/check-probe.ps1; ./tools/run-probe.ps1 -TimeoutSeconds 20
```

`tools/xenia-launch.ps1` is the shared launcher: it checks the emulator
hash, isolates the storage root under `build/xenia-storage`, answers the
stock first-run disclaimer dialog with "No", closes the window at the
timeout and only kills the process it started if that is ignored. Each run
folder holds `command.txt` (exact command line and hashes), `xenia.log` and
`summary.json`.

Window captures: `./tools/run.ps1 ... -CaptureCount 4 -CaptureAfterMs 7000
-CaptureIntervalMs 800` saves `capture-N.png` of the emulator window's client
area and records the average colour of its central region in
`captures.json`; the images are the visual evidence, the averages the quick
check. `./tools/capture-pixels.ps1 -Image <png> -Points "560,360","720,360"`
reads individual pixels, `-Compare <png>` counts differing pixels between two
captures and `-Background "10,10"` prints the bounding box of everything that
differs from the pixel at that position (face size and aspect checks).

A long run is a short `-TimeoutSeconds` after late captures, because the
launcher waits for the timeout only after the last capture:
`./tools/run.ps1 -Target build/xenia-cube/helengine.xex -TimeoutSeconds 5
-CaptureCount 2 -CaptureAfterMs 5000 -CaptureIntervalMs 590000` runs ten
minutes with captures at 5 s and 595 s.

Useful emulator flags for diagnosis (append with `-ExtraArgs`):
`--log_level=3` (debug log), `--dump_shaders=<dir>` (microcode and
translated shader dumps), `--gpu=vulkan` (alternate host backend).
`tools/hostdbg.py --timeout 40 --log out.txt -- <xenia.exe> <flags> <xex>`
runs the emulator under a minimal debug loop and prints host exceptions that
the emulator's window loop would otherwise swallow.

Log markers written by the guest (all prefixed `(DbgPrint) HEL `): platform
init and tick frequency, every physical allocation with virtual/physical
address and size, the selected video mode and EDRAM tile budget, ring
initialization, `first present complete` with the first frame's ring size
and GPU wait, then every 60 frames the frame count, average CPU microseconds
per frame, the wall time of the last 60 frames, presented frames and the
owned allocation count and bytes, and the guest heap's live allocations and
bytes (all constant after initialisation: 5 physical allocations and 207
heap allocations for the cube with the core linked in). The core logs its
own progress as `HEL core: constructing -> constructed -> initializing ->
ready`, then the first update and draw. Before any of that, the guest runs
two checks that stop the demo on failure: the C++ environment self-test
(`HEL cxx:`) and the binary byte-order check for this big-endian target
(`HEL endian:`). Errors name the stage code
(`src/engine/frame.h`); a frame-limited build ends with `frame loop stopped
with=00000000` and `allocations after shutdown=00000000`.
