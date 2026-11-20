# Opening brief — KR32 session

**Created:** 2026-11-20T02:56:15-08:00
**Role:** implement stage A of the krc toolchain adoption in this repository only.
**Repository:** `C:\Users\kevin\source\repos\KR32`.
**Plan (authoritative):** `C:\Users\kevin\source\repos\krdbg\docs\requests\kr32-adopt-krc-toolchain.md`.
Read it before touching anything. Where this brief and the plan disagree, the plan wins.

## Before you start

- Start the session with its working directory at the repository root.
- The tree has a template sync in flight: modified `AGENTS.local.md`, untracked
  `AGENTS.md`, `.agents/`, `.dev-template.json` and `CLAUDE.md`. Finish that as
  its own commit, or revert it; do not carry it into the branch.
- Work on the existing `adopt-krc` branch, which tracks `origin/adopt-krc` and is
  based on `origin/main`. Do not commit to `main`.
- The ARM64 tester appends `docs/arm64-test-log.md` and pushes to this branch.
  Pull with rebase before pushing, and never force-push: that would invalidate
  the tester's last-tested state.
- **Dependency:** the KRCompiler branch must already carry plan items 4.1–4.5
  and 4.9. Ask the programme owner for the SHA; record it in the log and in the
  build setup. Do not vendor KRCompiler to unblock yourself.

## Scope

Stage A only: items 3.1–3.8 — the debug spike, the C++ corpus check, the krc
branches of `compile_c_source` and `compile_cxx_source` behind
`KR32_TOOLCHAIN=krc` (default `builtin`), option and diagnostic mapping, the
listing from facade rows, the OS-mode smoke test, the guest headers as files, and
the suite run under both toolchains.

Do not: flip the default or delete `src/cc`, `src/cxx`, `src/asm`, `src/link`
(3.9), change the debugger, rebuild the kernel/BIOS, or edit KRCompiler, kr16 or
krdbg.

## Design decisions already made

- A documented C++ subset is acceptable in stage A. Classify every rejection;
  stage B needs a clean corpus or a written sign-off listing the permanent
  differences, including the deliberate `throw <scalar>`.
- The debugger keeps reading `.debug_info`; the krc image carries it. No KDBG
  work in this repository for this change.
- The kernel and BIOS images stay as shipped, and `kr32 gui --os [kernel.bin]
  [--bios bios.bin]` plus `--embedded` keep working. 3.6 tests both.
- The listing is formatted from the facade rows, not reconstructed.

## Build and gate

- `cmake --preset windows-ninja`, then `cmake --build --preset windows-ninja`,
  then `ctest --preset windows-ninja`.
- WSL uses the repository's own gate: `bash scripts/run-gate.sh`, per
  `AGENTS.local.md`.
- Run the suite twice, once per `KR32_TOOLCHAIN` value.
- 3.1 is a spike before any repository change: a krc-built `.k32` must debug in
  the current GUI. If it does not, stop and report.

## Log (required)

- Keep `docs/adopt-krc-log.md` in this repository. Append-only, committed with
  the work.
- Every entry begins with the exact `iso` string from `local-datetime`, called
  immediately before writing that entry.
- Each entry records: what changed, the command run and its real result, the
  commit, the next step, and any decision or blocker.
- Never rewrite an earlier entry. Correct it with a new one.

## Report back

Branch name, HEAD SHA, the pinned KRCompiler SHA, the gate command and its real
output, the C++ corpus result, the log path, and any blocker. If the plan is
wrong, stop and report; the programme owner changes it. Do not edit krdbg or the
other two repositories.
