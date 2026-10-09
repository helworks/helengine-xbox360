# X360 shader frontend probe report

## Scope

The probe compiles an authored HLSL file directly to SPIR-V for the vertex or pixel stage. This change does not add Xenos lowering, include resolution, preprocessor defines, or configurable compiler options.

## Target environment correction

Shaderc's [`env.h`](https://github.com/google/shaderc/blob/main/libshaderc/include/shaderc/env.h) defines `shaderc_env_version_vulkan_1_0` as `((1u << 22))`, or `0x00400000`. The probe now passes that value to `shaderc_compile_options_set_target_env`. The C API documents `version` as the target environment version. A value of `1` had fallen through to Shaderc's default version; Vulkan's default is also 1.0, so compilation could appear correct despite the incorrect enum value.

The real-compiler test checks the emitted SPIR-V header version (`1.0`) and the reported target enum (`0x00400000`).

## Identity

The CLI identifies itself as `shader-probe 0.1.0` and reports the SHA-256 of the native Shaderc library it loads. It does not claim to be Silk.NET: the probe calls the Shaderc C API through `ctypes` and does not load the Silk.NET managed binding.

Tested native library:

`C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll`

SHA-256:

`21ecc34ff804135d9d25d31c576a2f46d1a26b780fa1404b9d3fdc86db3c67dc`

## Output failure semantics

Before clearing an explicit output, the CLI resolves it and rejects it if it names the input source or native Shaderc library. For a distinct output path, it removes any old artifact before compiling, so compiler failure cannot leave an earlier SPIR-V file looking current. Failed output writes also clean up a partial output.

## RED/GREEN evidence

RED command, before the fixes:

```powershell
py -3 tests/shader_frontend_test.py
```

Result: 9 tests ran; 4 failed on the expected regressions: the compile and `--version` output falsely identified Silk.NET, a failed compile retained a stale output, and output equal to the source overwrote that source.

GREEN command, after the fixes:

```powershell
py -3 tests/shader_frontend_test.py
```

Result: 9 tests passed using the installed native compiler.

Identity probe command:

```powershell
py -3 tools/shader-probe/shader_probe.py --version --shaderc 'C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll'
```

Output:

```text
shader-probe 0.1.0; shaderc native SHA-256 21ecc34ff804135d9d25d31c576a2f46d1a26b780fa1404b9d3fdc86db3c67dc
```

Compile command shape tested by the suite:

```powershell
py -3 tools/shader-probe/shader_probe.py <source.hlsl> --stage vertex --entry main --output <output.spv> --shaderc 'C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows\runtimes\win-x64\native\shaderc_shared.dll'
```

## Dependency setup invalidation follow-up

The compile path now resolves the explicit output against both source and Shaderc paths before dependency loading. If the paths are distinct, it removes the old output before loading or hashing the native library. The `--version` path returns before output handling, even if an `--output` argument is also present. A native loader failure now includes the library path and load error.

RED command, before the dependency-order fix:

```powershell
py -3 tests/shader_frontend_test.py
```

Result: 12 tests ran; 3 failed as expected. Missing-library setup left stale output behind, the source/output collision was masked by that missing-library error, and invalid-DLL diagnostics lacked the library path/context. The `--version` output-preservation assertion passed.

GREEN command, after the dependency-order fix:

```powershell
py -3 tests/shader_frontend_test.py
```

Result: 13 tests passed. The real-tool cases verify missing and invalid native library setup, stale-output invalidation, source/library path protection before loading (including a missing library path), and that `--version` leaves an explicitly named output untouched.
