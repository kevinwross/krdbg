# Opening brief — KRCompiler session

**Created:** 2026-11-20T02:56:15-08:00
**Role:** implement stage A of the krc toolchain adoption in this repository only.
**Repository:** `C:\Users\kevin\source\repos\KRCompiler`.
**Plan (authoritative):** `C:\Users\kevin\source\repos\krdbg\docs\requests\krcompiler-toolchain-package.md`.
Read it before touching anything. Where this brief and the plan disagree, the plan wins.

## Before you start

- Start the session with its working directory at the repository root, so this
  repository's `AGENTS.md` and `AGENTS.local.md` load.
- `master` is **7 commits ahead of `origin/master`** and the tree is clean. Do
  not branch until that is settled: either those commits are pushed, or the
  owner tells you to include them. Base the work branch on the pushed default
  branch, not on a divergent local `master`.
- Work on the existing `adopt-krc` branch, which is based on the pushed `master`
  and already carries `docs/arm64-tester.md`. Do not commit to `master`.
- The ARM64 tester appends `docs/arm64-test-log.md` and pushes to this branch.
  Pull with rebase before pushing, and never force-push: that would invalidate
  the tester's last-tested state.

## Scope

Stage A only: items 4.1–4.11 of the plan — the two public headers, the two CMake
packages, the facade, the in-memory and determinism rules, the CLI rebuild, the
listing model, `.kdbg` in `.kex`, the embedding document, KR16 debug-info
emission, and the KR16 assembler directives.

Do not: add KDBG v1.1 records, remove the `.debug_info` bridge, change codegen or
optimization policy, or edit KR32, kr16 or krdbg.

## Design decisions already made

- `krc::debug` ships in this change (4.7).
- The listing model is part of the facade (4.9); hosts format their own `.lst`.
- Static libraries only.
- `.kdbg` in `.kex` uses absolute addresses, with the image base as a
  little-endian `u64` in the header's reserved bytes at offset 56; the reader
  rejects a mismatch. `docs/symbols.md` and ADR 0009 are amended in the same
  change that writes the field.
- The facade is `krc::toolchain`; the loader is the host's. Do not add a
  process boundary or a filesystem dependency to the facade.

## Build and gate

- Inner loop: `cmake --preset windows-msvc-debug`, then
  `cmake --build --preset windows-msvc-debug --parallel`, then
  `ctest --preset windows-msvc-debug --output-on-failure`.
- This change crosses `CMakeLists.txt`, `CMakePresets.json`, `include/krc/` and
  the symbol format — boundary categories under `AGENTS.local.md`. Run the
  matrix before the first push, and record it per that file's gate format.
- The executed tests need the sibling VMs (`KRC_KR32_VM`, `KRC_KR16_VM`). If they
  are absent, say so; do not silently skip the executed tests.

## Log (required)

- Keep `docs/adopt-krc-log.md` in this repository. Append-only, committed with
  the work.
- Every entry begins with the exact `iso` string from `local-datetime`, called
  immediately before writing that entry.
- Each entry records: what changed, the command run and its real result, the
  commit, the next step, and any decision or blocker.
- Never rewrite an earlier entry. Correct it with a new one.

## Publish for the other sessions

- Push the branch as soon as 4.5 and 4.9 are on it: kr32 pins that SHA.
- Push again when 4.6 lands: kr16 pins that SHA.
- Report the SHA in the log and to the programme owner.

## Report back

Branch name, HEAD SHA, the gate command and its real output, the log path, and
any blocker. If the plan is wrong, stop and report; the programme owner changes
it. Do not edit krdbg or the other two repositories.
