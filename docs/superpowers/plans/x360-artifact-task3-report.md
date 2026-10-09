# X3SP Task 3 report

Status: DONE

## Scope and files

Implemented the Task 3 pure C semantic parser and borrowed-view accessors for
the approved X3SP v1 schema. The parser reuses the Task 2 envelope and SHA-256
implementation; shared section/error types now have one public definition.

Created:

- `src/platform/xenia_guest/x360_shader_artifact.h`
- `src/platform/xenia_guest/x360_shader_artifact.c`
- `src/platform/xenia_guest/x360_shader_artifact_semantics.c`
- `tests/shader_artifact_test.c`

Modified:

- `src/platform/xenia_guest/x360_shader_artifact_internal.h`
- `tools/test-shader-artifact.ps1`

## Implementation and coverage

- Parse validates the Task 2 envelope/integrity first, then validates stage
  ownership and complete table partitioning, strict canonical UTF-8 pool
  references, stage/profile/layout rules, interfaces and pair linking,
  resources, constant declarations and used accesses. It computes control and
  upload costs and publishes the borrowed view only after full validation.
- Accessors return bounded spans or decode little-endian words/u32 values
  bytewise. They clear provided outputs on failure, check bounds/UTF-8, and
  tolerate unaligned source data.
- Tests cover the hand-derived golden fixture and every truncation; its
  control/cost values are `0/22`. A second hand-authored test fixture covers
  independent VS/PS constant banks, texture/sampler pairing, differing PS
  profile provenance, exact repeated-string reuse, and sparse Batch2D input at
  location 5 with stride 24 and offset 20. It also proves an unused matrix
  declaration is accepted without an access orientation.
- Repaired-integrity mutations cover unknown profile/resource/format/major
  values, bank and fetch-slot errors, wrong interface widths/strides/offsets,
  missing position/color, varying mismatch, wrong-section and hidden ranges,
  duplicate resources/names, matrix decorations, declaration overlap, used
  access aliasing and foreign-stage references, malformed/aliased/overlong
  strings, invalid Unicode scalar encodings, and unreferenced trailing pool
  bytes. Failures assert the error class and a cleared view; failure helpers
  also verify the input bytes remain unchanged.
- Accessor cases cover first/last/one-past records and words, nulls, huge
  offsets, the `(0,0)` empty string, a UTF-8 split at a multibyte boundary,
  length limits, and an alignment-plus-one u32 span.

## TDD and verification evidence

The initial attempted RED command was
`gcc -std=c11 -Wall -Wextra -Werror -I src/platform/xenia_guest -o .artifacts/x360-artifact-task3-before/task3-red tests/shader_artifact_test.c src/platform/xenia_guest/x360_shader_artifact_sha256.c src/platform/xenia_guest/x360_shader_artifact_envelope.c`.
It did not execute because GCC is absent from the PowerShell environment; the
default sandbox also could not access Docker Desktop's engine pipe. No
preimplementation semantic RED was captured, and those environment failures
are not presented as one.

Actual executable semantic REDs were captured later with repaired-integrity
mutations:

- `powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite Semantic`
  exited 134 in
  `.artifacts/shader-artifact/20260925T211145296Z`: unsupported fetch-format
  enum `1` expected `UNSUPPORTED` (5), got `SEMANTIC` (6), diagnostic offset
  592.
- The same command exited 134 in
  `.artifacts/shader-artifact/20260925T211217289Z`: unsupported matrix-major
  enum `3` expected `UNSUPPORTED` (5), got `SEMANTIC` (6), offset 1116. The
  fetch-format case passed after its classification correction.
- After both corrections and the complete semantic mutation set,
  `powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite Semantic`
  passed host, AddressSanitizer, UndefinedBehaviorSanitizer, and PPC compilation
  in `.artifacts/shader-artifact/20260925T211456363Z`.

Fresh final verification:

`powershell -NoProfile -File tools/test-shader-artifact.ps1 -Suite All`

Exit code 0; output directory
`.artifacts/shader-artifact/20260925T211737162Z`. The Task 2 envelope and Task 3
semantic/accessor host binaries passed. AddressSanitizer and
UndefinedBehaviorSanitizer were available and both binaries passed under each.
All four PowerPC freestanding objects (SHA, envelope, parser, semantics)
compiled. The run retained host logs, sanitizer probe logs, and all four
sanitizer test logs in that output directory. PowerShell syntax parsing and
`git diff --check` for the modified tracked files also passed.

## Snapshots and limits

The before snapshots under `.artifacts/x360-artifact-task3-before` were checked
byte-for-byte before edits. Their SHA-256 values are:

- `src/platform/xenia_guest/x360_shader_artifact_internal.h` —
  `85F7FFFDF56D512F1EFD337185765E0E1C8334700CEF119C969C49FC747BE90F`
- `tools/test-shader-artifact.ps1` —
  `522F7F3AE5C2B18EA629F1DC27278936D5A8231CC8F6017D5FC04F143010D356`

The harness preserves the pinned Docker image, no-pull/offline run, base64
PowerShell-to-bash transport, `/tmp` sanitizer executables, non-PIE ASan,
bounded probes/timeouts, and now copies actual sanitizer test output to `/out`
before reporting success or failure.

This parser does not perform full ISA validation, authenticate hashes, emit
PM4, promise whole-frame fit, integrate runtime call sites, or claim GPU or
console execution. PowerPC was compile-checked only. No dependencies, commits,
worktrees, emulator runs, or shared Engine/PS3/runtime source edits were made.
