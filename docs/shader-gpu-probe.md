# Shader GPU probe (opt-in)

This diagnostic builds real HLSL-derived Xenos programs into a separate image.
It is not the shared material/cooker integration, and a successful build is
not proof of correct GPU rendering.

## Build

From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build-shader-gpu-probe.ps1 -Shaderc C:/dev/helworks/helengine-wiiu/builder/bin/Debug/net9.0-windows/runtimes/win-x64/native/shaderc_shared.dll
```

The wrapper defaults to the authored core, content and catalog previously used
for the text validation. It accepts `CoreGenerated`, `CoreLib`, `ContentRoot`
and `CatalogRoot` overrides. It does not launch Xenia.

- Image: `build/shader-gpu-probe/image/helengine.xex`.
- Generated programs/constants: `build/shader-gpu-probe/probe_shaders.h`.
- Source/compiler/program identities: `build/shader-gpu-probe/probe_manifest.json`.
- Image hashes and reproduction command: `build/shader-gpu-probe/build-record.txt`.

The image has `HEL_SHADER_GPU_PROBE=1`. Normal builds do not enable it. The
wrapper invalidates only its own XEX before building, and keys its native
object cache by the full generated-header hash. It does not overwrite the
normal image or the previously validated text image.

## What to check when launching is authorized

The ordinary authored scene/UI draws first. A single small, skewed textured
quad is then added near the center. It uses a nonsymmetric generated transform,
RGB tint and squared UV coordinates. Its 2x2 texture has four distinct colors
and a half-alpha texel; existing linear filtering smooths the transitions.
The shaders are active only for that draw, and the legacy shader cache is
invalidated afterwards. Verify the scene, text and FPS still display correctly.

Observe sustained frames, not just the first image. Expected logs include:

- `HEL shader probe:` followed by the generated identity and program sizes.
- `HEL shader probe: presented frames` every 60 completed presentations.
- `HEL shader probe: 600 presented frame marker` after 600 presentations.
- Existing first-frame ring usage, with no ring-full or fence-timeout error.

The initial generated pair costs 100 dwords for program/constant packets,
plus 10 SQ state dwords and the draw's state/descriptors. The existing ring
overflow guard remains unchanged. The old scene's measured ring usage is not
a measurement of this new image; verify the new log after execution.

## Verified on 2026-09-23 (before GPU execution)

- Python suite: 60 tests passed, including compiler and pair validation.
- Native C packet harness: normal, invalid-ucode and zero-constant cases passed.
- Changed C units compiled for PowerPC with the diagnostic enabled and disabled.
- Separate XEX built and passed `xexcheck`; repeated cached build was identical.
- Missing compiler DLL invalidated only the diagnostic XEX; rebuilding restored it.
- XEX SHA-256: `BE25AE48683E834B43611C96A3A6DCDEB34E5A4EE091DE77DF8776D152FFB8A5`.

The first full build emitted 18 unused-parameter warnings in the existing
generated `runtime/native_exceptions.hpp`. The modified C units compiled with
warnings treated as errors. No emulator, screenshot, desktop control or physical
console validation was used for the results above.

## GPU execution verified on 2026-09-23

After explicit authorization, the same XEX was launched through Xenia's command
line without capture, desktop automation or forced window activation. The user
confirmed seeing the textured quad with the scene and text ("vejo").

Log: `build/logs/shader-gpu-20260923-185428/xenia.log`.
It records the expected generated identity, the 600-presented-frame milestone,
and continued presentations beyond 6,600 frames. First-frame ring usage is
`0x1E68` (7,784 dwords), leaving 408 below the 8,192-dword safe budget.
The inspected log contains no ring-full, fence-timeout or overflow match.

Gate A is satisfied for this bounded transform/color/2D-texture pair in Xenia.
This is not physical Xbox 360 validation, a pixel-exact visual comparison,
full Standard Shader support, or shared material/cooker integration.
