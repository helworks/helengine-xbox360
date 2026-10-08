# Xbox 360 managed shader backend

The host-side `Xbox360ShaderBackend` registers for the shared Xbox 360 shader
target through `Xbox360PlatformAssetBuilder.RegisterShaderBackends`. Registry
construction is lazy: Python and shaderc are resolved only when a shader is
compiled. This backend deliberately does not implement the legacy target
provider and does not change editor target selection, material cooking, runtime
activation, or the guest ABI.

The backend currently accepts shader model 4.0 vertex and pixel requests,
requires binding `DefaultSpace` to be zero, and opts out of the shared compile
cache. Every request starts one host protocol process so changed include/tool
contents cannot be hidden by stale service-cache entries.

## Host tools

By default, the backend launches `python` (or `HEL_X360_PYTHON`) and loads
`shader-tools/shader-probe/backend_protocol.py` relative to the builder
assembly. It bundles the protocol's Python dependencies beside that assembly.
The shaderc DLL is supplied explicitly using `HEL_X360_SHADERC` or
`SHADERC_SHARED_LIBRARY`; callers and tests may instead pass all paths through
`Xbox360ShaderCompilerOptions`. The backend does not search sibling platform
outputs for a DLL. Missing tools are reported on compile, not while registering
the backend.

Compilation uses protocol 1 JSON-lines over redirected UTF-8 standard streams.
Messages are bounded to 8 MiB including newline, include requests to 4096, and
the process timeout defaults to 60 seconds. On protocol failure or timeout the
compiler terminates only the child it started. Include resolution follows the
provided synchronous `IShaderIncludeResolver`; the process timeout cannot
interrupt arbitrary user callback code while that callback is executing.

## Results and current ABI boundary

Successful results contain the host-lowered big-endian Xenos bytecode and the
compiled module's reflected interfaces/resources. Reflection uses `LOCATION`
indices for explicit interface locations and `SV_POSITION` for SPIR-V
BuiltIn Position; it does not reconstruct arbitrary original HLSL semantics.
Resource logical slots use the request's constant-buffer, texture, and sampler
shifts, while raw set/binding data stays in host metadata. Warnings are
preserved as shared warning diagnostics.

`Xbox360ShaderCompileResult` additionally exposes native metadata JSON, tool
and compile identities, and the source names/hashes of includes consumed by
shaderc. Those host-only fields are for later cooker work and are not a final
guest serialization contract. Unsupported compiler/lowering features fail
with request context rather than receiving placeholder binaries or reflected
bindings.
