# X3SP Task 2 report

Status: DONE

Scope: private pure-C SHA-256 and X3SP v1 envelope validation, with a focused
host/PowerPC harness. The C# writer and existing runtime call sites are outside
this task and remain untouched.

Approved inputs: `docs/superpowers/plans/x360-artifact-task2-brief.md`,
`docs/superpowers/specs/2026-09-25-x360-shader-artifact-design.md`, and
`docs/superpowers/specs/2026-09-25-x360-shader-artifact-wire.md`.

Task 1 fixture contract: `tests/shaders/artifacts/minimal-v1.x3sp`, 668 bytes;
whole-file SHA-256
`698ABA97A517880A854B93378473843285F108851E72C1A3D1BA5D3AC09071C6`.
This whole-file digest is distinct from the virtual-zero integrity digest
stored in the fixture header.

## Implementation record

Implemented the private fixed-storage SHA-256 helper and pure-C envelope
validator. The parser checks header/version/target/flags, exact eight-section
directory ordering and lengths, count/range bounds before multiplication,
zero padding and exact file coverage. It hashes the full artifact with the
integrity field virtually zeroed, validates both stage code ranges before
hashing either stage, and hashes each decoded little-endian word in compiler
big-endian digest order. It writes the borrowed view only after all envelope
and integrity checks pass; failures leave the output view zeroed.

The focused harness covers standard SHA vectors, padding and update boundaries,
input immutability, the accepted unaligned 668-byte fixture, its independent
whole-file digest, every truncation, repaired-checksum structural mutations,
range/overflow cases, output clearing, and stage digest byte order.

Files changed:

- `src/platform/xenia_guest/x360_shader_artifact_internal.h`
- `src/platform/xenia_guest/x360_shader_artifact_envelope.c`
- `src/platform/xenia_guest/x360_shader_artifact_sha256.h`
- `src/platform/xenia_guest/x360_shader_artifact_sha256.c`
- `tests/shader_artifact_envelope_test.c`
- `tools/test-shader-artifact.ps1`

## Verification record

Requested command:

```powershell
powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite Envelope
```

The first PowerShell RED attempt did not invoke a compiler because host `gcc`
was not on that PowerShell process's PATH. It was not counted as a test result;
the pinned-container RED run then compiled the test and failed to link on the
not-yet-implemented SHA symbols, as intended. SHA GREEN passed the empty,
`abc`, 55/56/63/64/65-byte and 130-byte multiblock vectors, split updates, and
input immutability checks. An added whole-file digest assertion initially had
an extra `a` in its expected literal; Python `hashlib` and PowerShell
`Get-FileHash` confirmed the fixture's supplied digest, and the test literal
was corrected.

The first harness invocation passed a multiline command through Windows
PowerShell's native argument handling incorrectly: it printed only `Host` and
left its output directory empty. The harness now base64-transports that static
container script. A repaired-checksum unknown-kind test then failed as
expected with the parser's generic envelope code; the parser was corrected to
return the unsupported code for unknown kinds. A repaired-checksum out-of-file
section-offset test also failed before the bounds/order checks were reordered;
it now returns the range code. Both fixes were followed by focused GREEN runs.

Failed sanitizer attempts and handling:

- One run hung starting the AddressSanitizer probe from the mounted output
  directory. `docker top` showed that probe in `D` state and `/proc` reported
  `p9_client_rpc`; only that Task 2 container was stopped (exit 137). No
  sanitizer test or PPC compile was reached in that attempt.
- A later default-PIE AddressSanitizer test process emitted repeated
  `AddressSanitizer:DEADLYSIGNAL` lines without a diagnostic. The same focused
  test built with `-fno-pie -no-pie` passed. The harness uses those flags for
  the AddressSanitizer probe and test, runs sanitizer binaries from container
  `/tmp`, bounds probes at 10 seconds and tests at 30 seconds, and captures
  output so a failure remains concise.

Final fresh command completed with exit code 0:

```text
Suite: Envelope
Host compiler: gcc (Debian 12.2.0-14+deb12u1) 12.2.0
PowerPC compiler: powerpc-eabi-gcc (devkitPPC) 15.2.0
shader artifact SHA/envelope tests passed
address: available; running focused harness
shader artifact SHA/envelope tests passed
undefined: available; running focused harness
shader artifact SHA/envelope tests passed
PowerPC freestanding objects compiled
```

Command: `powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite
Envelope`. Docker used the pinned image with `--pull=never --network none`, a
read-only `/work` source mount, and an isolated `.artifacts/shader-artifact`
output mount. Final output directory:
`.artifacts/shader-artifact/20260925T202854952Z`. The run completed without a
pending process. Earlier output directories are retained and correspond to
the failed/intermediate attempts above.

The focused run rejected all 668 truncations; accepted the unaligned golden
fixture; confirmed its whole-file SHA-256; rejected repaired-checksum header,
directory, count, length, padding and stage-range mutations with the expected
codes; checked both stage ranges before comparing stage digests; and verified
that every failed parse leaves the view fully cleared.

Known preflight from the task brief: host GCC 12.2.0 is available; the pinned
DevKitPro PowerPC compiler is available at
`/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc` but is not on the default bash
PATH. Docker image `devkitpro/devkitppc@sha256:db8e39c73546d461d5606dc7dfed462a7d5957498502562e3b46769d02496cc1`
is already installed; the approved invocation requires `--pull=never
--network none`, a read-only source mount, and a separate in-checkout output
mount. Docker execution requires elevation in this environment.

## Scope and limits

The parser borrows the immutable caller buffer, allocates nothing, performs no
I/O or platform calls, and publishes no partial view on failure. Envelope
success is not a public usable shader view. Task 3 semantic validation, full
ISA validation, authenticity, GPU validation, cooker/runtime integration, and
full-project Python/.NET suites were outside this Task 2 run. No dependencies,
commits, worktrees, emulator runs, captures, UI automation, or shared
Engine/PS3/runtime edits were made.

The checkout contains unrelated dirty edits. They are preserved and excluded
from this task.
