# Xenia Compatibility and Open Toolchain Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to execute task-by-task after the user authorizes implementation. Delegation remains subject to the user's multi-agent opt-in rules. All steps below are future work.

**Goal:** Prove repeatable open-toolchain PowerPC execution inside Xenia and establish the concrete GPU, presentation, and shader interfaces needed for the cube.

**Architecture:** Keep a freestanding probe separate from engine code. Inspect a pinned Xenia revision and test each compatibility assumption with observable evidence. Publish a backend contract before beginning the rendering plan.

**Tech Stack:** PowerShell orchestration on the Windows host; an openly licensed PowerPC compiler/linker selected by evidence; a pinned Xenia build. The compiler, assembler, and runtime are discovery outputs, not assumed installed dependencies.

**Spec:** [Accepted design](../specs/2026-09-15-xenia-unlit-cube-design.md).

## Global constraints

- Xenia on PC is the first execution target.
- A homebrew-capable Xbox 360 is available for subsequent validation.
- No Microsoft XDK dependency, including compiler, headers, libraries, shader tools, or samples.
- Use openly licensed tools and record dependency provenance. Open-source copyleft build tools are acceptable; their license is distinct from the engine's license.
- Do not assume the first Xenia executable will also boot through XeLL unchanged.
- Runtime libraries and shader binaries need documented source and licensing; publicly downloadable code is not automatically a reusable dependency.
- Execution is on hold. This plan authorizes no installation or code execution by itself.

## Task 1: Record the reproducible environment

**Create:** `docs/xenia-compatibility.md`, `tools/config.example.json`, `.gitignore`, `third_party/manifest.toml`.

**Consumes:** The existing research docs, local tool inventory, and the user-provided Xenia installation path when it cannot be found in project configuration.

**Produces:** An environment record containing Xenia variant/revision, executable SHA-256, host GPU/backend, compiler/linker versions and targets, build-shell choice, source revisions, license references, and exact invocation commands. Personal absolute paths stay in ignored `tools/config.local.json`.

- [ ] Inspect existing tools without installing or upgrading anything. These are inventory commands, not build validation:

```powershell
Get-Command clang, ld.lld, llvm-readelf, llvm-objdump, gcc, make, cmake, wsl -ErrorAction SilentlyContinue | Select-Object Name, Source
```

- [ ] Locate the user's chosen Xenia executable and record its hash and visible version. Inspect source matching that build rather than treating upstream master as the installed version.
- [ ] Inspect the compiler's supported targets and linker formats. Compare an existing GNU Xenon cross-toolchain against an LLVM PowerPC route; reject a candidate if it cannot emit the loader's required machine/endianness/format or support the selected entry ABI.
- [ ] Record required downloads/builds as explicit setup steps with pinned sources. Choose the smallest working route; do not build an entire SDK as incidental setup.
- [ ] Add each selected third-party component's source revision, source hash where applicable, license, modifications, redistribution status, and role to the manifest. Do not select a runtime with unresolved reuse rights.
- [ ] Add ignore rules for local configuration, downloaded toolchains, build output, emulator logs, and generated evidence; preserve authored source and concise result summaries.
- [ ] Verify the report distinguishes discovered tools from successfully tested tools, then commit as `docs: record Xenia development environment`.

## Task 2: Establish the loader and entry contract

**Modify:** `docs/xenia-compatibility.md`.

**Consumes:** The pinned Xenia source from Task 1.

**Produces:** A source-linked loader/ABI table and a selected probe observation method.

- [ ] Trace `src/xenia/cpu/elf_module.cc`, `src/xenia/kernel/user_module.cc`, and the guest thread creation path. Record machine type, endianness, accepted addresses, segment alignment, BSS handling, entry point, initial registers, stack ownership, and return behavior.
- [ ] Identify whether kernel imports are available to the ELF path and how they are resolved. Do not infer import support from the presence of an ELF loader.
- [ ] Select an allowed mapped writable address for the probe's result symbol using the inspected loader rules. Select a way to observe that symbol through the pinned build's debugger or trace facilities.
- [ ] Record a failing/control case: an invalid machine type or invalid segment address must be rejected. Keep malformed-file testing bounded to a disposable process and known loader checks.
- [ ] Verify each row against an exact source revision and function, then commit as `docs: define Xenia guest loader contract`.

## Task 3: Build and observe the minimal guest probe

**Create:** `probes/entry/probe.c`, `probes/entry/start.S`, `probes/entry/link.ld`, `tools/build-probe.ps1`, `tools/run-probe.ps1`, `tools/check-probe.ps1`.

**Modify:** `docs/xenia-compatibility.md`.

**Consumes:** Task 2's loader/ABI table and Task 1's compiler.

**Produces:** `build/probe/entry.elf`, its link map, disassembly, launch log, and proof of guest execution.

- [ ] Implement the probe body with no library calls. This is its complete intended C behavior; startup and linker definitions must be derived from the inspected ABI, not from a XeLL sample:

```c
volatile unsigned int hel_probe_result = 0;
void hel_probe_main(void) {
    hel_probe_result = 0x48454C30u;
    for (;;) { }
}
```

- [ ] Write `start.S` to enter `hel_probe_main` with the verified stack/register convention and `link.ld` to place code/data/BSS in permitted memory. Document any small-data or relocation requirements. Disable unneeded compiler runtime dependencies; reject unexplained unresolved symbols.
- [ ] Make `build-probe.ps1` accept `-Config`, invoke the selected compiler/linker with checked exit codes, and emit the ELF/map/disassembly under `build/probe`. Preserve exact flags in the script.
- [ ] Make `check-probe.ps1` inspect ELF headers/segments and assert the Task 2 rules, the named entry/result symbols, and absence of unexpected external runtime dependencies.
- [ ] Make `run-probe.ps1` accept `-Config` and `-TimeoutSeconds` (default 30), launch the pinned Xenia with logs isolated per run, and terminate only the process it launched on timeout. This probe intentionally loops; timeout alone is not failure or success.
- [ ] Observe `hel_probe_result` changing from zero to `0x48454C30` using the chosen guest inspection method. Record the target byte order and symbol address. A loaded module or black window does not pass this test.
- [ ] Repeat three launches. Capture the marker result and logs for each. If observation requires a diagnostic emulator build, label it clearly and retain a separate stock-build compatibility result.
- [ ] Commit only authored probe/scripts/report as `feat: prove open-toolchain guest execution in Xenia`.

Planned invocations after the scripts exist:

```powershell
./tools/build-probe.ps1 -Config tools/config.local.json
./tools/check-probe.ps1 -Config tools/config.local.json
./tools/run-probe.ps1 -Config tools/config.local.json -TimeoutSeconds 30
```

## Task 4: Resolve graphics, presentation, and shader interfaces

**Create:** `docs/xenia-backend-contract.md`.

**Modify:** `docs/xenia-compatibility.md`, `third_party/manifest.toml`.

**Consumes:** Verified guest execution, pinned Xenia GPU/kernel source, public hardware references, and candidate open shader tools.

**Produces:** A complete backend contract used by the rendering plan.

- [ ] Trace guest-accessible GPU initialization and command submission. Record actual register/packet interfaces or imported functions, argument conventions, and how this executable format reaches them.
- [ ] Trace guest virtual/physical/GPU address translation, allocation alignment, resource visibility, completion fences, and command buffer ownership.
- [ ] Trace the path from guest rendering to Xenia display presentation, including any emulator-specific swap packet or kernel operation. Distinguish it from physical display-controller programming.
- [ ] Define the initial color/depth allocation, pitch/alignment, viewport, resolve destination, and presentation requirements. Calculate padded eDRAM use rather than relying only on raw pixel counts.
- [ ] Identify a buildable open route for position-transform and color-output Xenos shaders. Confirm native instruction format, bindings, vertex fetch layout, constants, and licensing. If a minimal assembler is necessary, estimate and specify that bounded component before writing it.
- [ ] Record each interface as: caller, exact signature or packet layout, source revision/function, preconditions, outputs, synchronization, failure behavior, and hardware portability assumptions.
- [ ] Classify the result as `ready-for-clear`, `bounded-extension-required`, or `blocked`. For the latter two, list the exact missing capability and smallest next experiment. Do not silently substitute a PC renderer or proprietary compiler.
- [ ] Update the rendering plan with exact backend calls, shader generation commands, and executable build commands from this contract. Only `ready-for-clear` releases its first implementation task.
- [ ] Commit as `docs: establish Xenia graphics backend contract`.

## Completion evidence

The compatibility report must link to the environment record, source-linked ABI table, three successful probe observations, dependency manifest, and backend contract. Shader generation and GPU interfaces remain hypotheses until the triangle and presentation milestones exercise them; do not label this milestone a working renderer.

## Handoff

Continue with [Video to cube](2026-09-15-01-video-to-cube.md) only after its prerequisites are met and implementation is authorized. If discovery demonstrates a larger SDK/emulator project is required, stop at the documented finding and revise scope with the user.
