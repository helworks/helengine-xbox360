# cube_test shared-shader build

Use the demodisc's existing `scenes/rendering/cube_test.helen`; do not create a
replacement scene. `tools/build-authored.ps1 -CubeSharedShaders` prepares only
the staged project copy:

- Keeps the authored scene, camera, spin script, UI and common material unchanged.
- Adds `CubeTestSolid.hasset.x360.hasset`, selecting `x360-mesh-shader-v1`.
- Copies the tested MeshMaterialV1 HLSL to `assets/shaders/X360CubeMesh.hlsl`.
- Uses the editor's real override serializer and shader asset identity rules.
- Lets the existing shader cooker package the mesh pair and renderer-owned
  SpriteBatch Textured pair. No emulator is launched.

Publish the current editor into a **build-local directory** first. The authored
build publishes codegen beside that editor, replacing that directory's previous
codegen copy. Do not point it at an editor installation another agent is using.
The codegen intermediate outputs stay beneath the authored build output.
The wrapper uses the published editor's `runtimes/win-x64/native/shaderc_shared.dll`
unless `HEL_X360_SHADERC` or `SHADERC_SHARED_LIBRARY` already selects a library.
The local scene selection explicitly overrides the demodisc-wide scene package.

Example from the repository root (PowerShell):

```powershell
dotnet publish C:/dev/helworks/helengine/helengine.ui/helengine.editor.app/helengine.editor.app.csproj `
  -c Debug -o .artifacts/cube-test-shared/editor `
  --artifacts-path .artifacts/cube-test-shared/dotnet

./tools/build-authored.ps1 `
  -ProjectRoot C:/dev/helprojs/demodisc `
  -EngineSourceRoot C:/dev/helworks/helengine `
  -EditorAppPath "$PWD/.artifacts/cube-test-shared/editor/helengine.editor.app.dll" `
  -CodegenRoot C:/dev/helworks/csharpcodegen `
  -OutputRoot "$PWD/.artifacts/cube-test-shared/authored-fresh" `
  -CubeSharedShaders
```

The output root must be fresh. Existing Xbox 360 overrides or a conflicting
`X360CubeMesh.hlsl` are rejected, not overwritten. This is an unlit, solid-color
mesh contract, not StandardShader lighting or textured 3D material support.

Preparation regression test, after publishing the editor above:

```powershell
dotnet run --project tests/cube-scene-preparation/cube-scene-preparation-tests.csproj
```

The executable test deserializes the emitted override with the actual editor
serializer, checks the literal shader identity/entrypoints, compares scene,
common material and HLSL bytes, and verifies overwrite rejection. It retains its
temporary fixture for diagnosis. Compilation and these tests do not establish
GPU correctness; that still requires user-observed execution.
