# Opening brief — kr16 session

**Created:** 2026-11-20T02:56:15-08:00
**Role:** implement stage A of the krc toolchain adoption in this repository only.
**Repository:** `C:\Users\kevin\source\repos\kr16`.
**Plan (authoritative):** `C:\Users\kevin\source\repos\krdbg\docs\requests\kr16-adopt-krc-toolchain.md`.
Read it before touching anything. Where this brief and the plan disagree, the plan wins.

## Before you start

- Start the session with its working directory at the repository root.
- `main` has a modified `src/resources/embedded_libc.cpp` and no other change.
  Commit it as its own change or stash it; do not carry it into the branch.
- Create `kr16-krc-toolchain` from `origin/main`. Do not commit to `main`.
- **Dependency:** the KRCompiler branch must already carry plan items 4.1–4.6
  and 4.9. Ask the programme owner for the SHA; record it in the log and in the
  build setup. If the package is not yet usable, stop and report — do not vendor
  KRCompiler to unblock yourself.

## Scope

Stage A only: items 2.1–2.5 — the spike, the `.kdbg` reader, the DAP reader, the
`build_program` krc branch behind `KR16_TOOLCHAIN=krc` (default `builtin`), and
the suite run under both toolchains.

Do not: flip the default, delete `src/cc`, `src/asm` or `src/link` (2.6), or edit
KRCompiler, KR32 or krdbg.

## Design decisions already made

- kr16's embedded libc stays; the krc path links it. If krc's codegen wants a
  runtime helper it lacks, the link fails at 2.4 — report it; krc's
  `targets/kr16/runtime/` is a documented exception, not a silent swap.
- The listing is formatted from the facade rows, keeping `-l` working.
- `.kdbg` addresses are absolute; the reader applies the plan's address rule.
- Do not add a second debug format to krc.

## Build and gate

- Configure `cmake --preset windows-ninja`, then
  `cmake --build --preset windows-ninja-debug`, then
  `ctest --preset windows-ninja-debug --output-on-failure`.
- Run the suite twice, once with `KR16_TOOLCHAIN=builtin` and once with `krc`.
- 2.1 is a spike before any repository change: if krc cannot produce a running
  kr16 image, stop and report rather than working around it.

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
output, the log path, and any blocker. If the plan is wrong, stop and report; the
programme owner changes it. Do not edit krdbg or the other two repositories.
