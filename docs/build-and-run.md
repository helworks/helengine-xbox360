# Build and run (Xenia guest)

Prerequisites: Docker with the `devkitpro/devkitppc` image, Python 3.11,
Windows PowerShell, the pinned Xenia Canary build, and
`tools/config.local.json` copied from `tools/config.example.json` with local
paths. Pinned revisions and hashes are in `xenia-compatibility.md`.

```powershell
# Engine image -> build/xenia/helengine.{elf,xex,map,dis,nm,readelf.txt,xex.json,hashes.txt}
./tools/build.ps1 -Config tools/config.local.json

# Demo modes: 0 clear (default), 1 triangle, 2 rotating cube; fixed cube angles 0..3
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-triangle -Define HEL_DEMO_MODE=1
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-cube -Define HEL_DEMO_MODE=2
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-cube-a2 -Define HEL_DEMO_MODE=2,HEL_DEMO_FIXED_ANGLE=2

# Shaders only (also run by build.ps1): shaders/*.xasm -> build/shaders
./tools/build-shaders.ps1 -Config tools/config.local.json

# Host tests: engine math, cube geometry, assembler encodings
./tools/test-host.ps1 -Config tools/config.local.json

# Failure-path build (unsupported video mode requested by the demo)
./tools/build.ps1 -Config tools/config.local.json -OutDir build/xenia-invalid -Define HEL_DEMO_INVALID_CONFIG=1

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
check.

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
per frame and the wall time of the last 60 frames. Errors name the stage
code (`src/engine/frame.h`).
