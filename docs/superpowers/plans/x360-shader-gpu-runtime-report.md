# X360 shader GPU runtime/build bridge report

## Changes

- Added macro-guarded probe shader validation, program upload, SQ metadata writes, stage constant uploads, and legacy program-cache invalidation in `src/platform/xenia_guest/shader_gpu_probe.inc`, included from `shader.c` only when `HEL_SHADER_GPU_PROBE=1`.
- Added the active-draw override in `mesh.c`; ordinary draws keep their existing shader and constant path. The override is cleared on error and completion. Diagnostic VS constants start at physical c0 and PS constants at physical c256; empty banks emit no packet.
- Added one small, centered, textured overlay quad in `src/demo/shader_gpu_probe.inc`. Its 2x2 texture has one half-alpha texel. The existing core scene draws first; completed-present counts are logged every 60 frames, with a marker at 600.
- Added the real-header-independent C ring harness and focused Docker runner. The controlled fixture uses 3-dword test programs and 2/3 constant dwords to check packet words and bank offsets.
- Added `tools/build-shader-gpu-probe.ps1`. It requires `-Shaderc` or `HEL_SHADERC_DLL`, uses the fixed diagnostic output directory, defaults to the authored-scene inputs, invalidates only the diagnostic XEX before fallible setup, checks generated header identity, and isolates the native object cache under the full header SHA-256. On success it records XEX/header/program hashes and the reproduction command in `build/shader-gpu-probe/build-record.txt`.

The currently generated real pair reports 51 VS dwords, 21 PS dwords, 20 VS constant dwords, no PS constants, and 100 upload dwords. That cost includes 6 program packet overhead dwords and 2 dwords for the one nonempty constant packet; it excludes the five SQ register writes (10 dwords) and draw state. This is build evidence only, not GPU behavior evidence.

The completed authored-scene build produced XEX SHA-256 `BE25AE48683E834B43611C96A3A6DCDEB34E5A4EE091DE77DF8776D152FFB8A5`; `xexcheck` reported zero failures. The generated header, vertex program, and pixel program SHA-256 values are `54280ab38640bcb3ae598535350fcd3348f7c2eedc3e1a78537ec4822b5efe5b`, `40c01033d5b097352583de0f004ff1d37d5640c456bcaadf1941d9b7cec284ef`, and `d70b3adafe4d38e2dee8c77e038b06b712836c58f28e48ce850a2795f547016a` respectively.

## RED/GREEN and verification

- RED was observed by the parent: compiling the new harness against the unmodified runtime failed on the missing `hel_shader_gpu_probe_*` APIs/header contract, as expected.
- After the logging-label and `-Werror` helper-guard fixes, the parent reports all three controlled-header C variants passed with zero failures: normal packet/cache/bank/overflow assertions, invalid ucode rejection, and zero-count constant banks.
- This session cannot access Docker Desktop's Linux engine. It was denied at `npipe:////./pipe/dockerDesktopLinuxEngine`; the parent can run the focused harness with:

  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/test-shader-gpu-runtime.ps1
  ```

- Both new PowerShell scripts passed the local PowerShell AST parser; the parent independently confirms the build wrapper parses cleanly.
- The parent reports the changed `main.c`, `shader.c`, and `mesh.c` compile with `-Wall -Wextra -Werror` for both `HEL_SHADER_GPU_PROBE=0` and `=1`, using the real generated header.
- The full wrapper build completed with exit code 0 using the authored core, 36 content files (8.3 MB), and generated catalog; `xexcheck` passed with zero failures. The parent is inspecting 18 compiler warnings from the existing authored core. No emulator was launched.
- The first successful invocation exposed a nested-array formatting bug in `build-record.txt`; the wrapper now flattens hash lines before composing the record, and the formatter was checked in PowerShell. The record from that earlier invocation predates the correction, so rerun the wrapper to refresh the output record. Its header-SHA-scoped object cache should reuse compiled objects for the same generated header.

## Reproduction command

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build-shader-gpu-probe.ps1 `
  -Config tools/config.local.json `
  -Shaderc "C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll" `
  -CoreGenerated build/from-editor/generated-core `
  -CoreLib build/resume-authored-core/libhelcore.a `
  -ContentRoot build/from-editor/content `
  -CatalogRoot build/resume-authored-catalog
```

## Limits

No emulator was launched and no capture was made. Compile and ring-harness evidence do not establish that the Xenos GPU displays the diagnostic correctly. The single-quad choice preserves headroom against the measured baseline of 7,632/8,192 first-frame dwords; no second patch or ring-budget increase was added.
