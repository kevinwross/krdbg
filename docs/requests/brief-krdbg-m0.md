# Opening brief — krdbg M0 session

**Created:** 2026-11-20T03:48:27-08:00
**Role:** implement milestone M0 of krdbg in this repository only.
**Repository:** `C:\Users\kevin\source\repos\krdbg`.
**Plan of record:** `PLAN.md` and `implementation.md`, both approved
2026-11-20T03:04:50-08:00. Read `implementation.md` §0 (how the checklists work),
§1 (status), §2 (M0), §9 (evidence log), §10 (external prerequisites) and §11;
and `PLAN.md` §3, §4, §8 and §11. `PLAN.md` wins where the two disagree.

## Before you start

- Start the session with its working directory at the repository root, so
  `AGENTS.md` and `AGENTS.local.md` load.
- Run the doctor once and record its real output:
  `.agents/templates/doctor.msvc.ps1`. It passed on this machine on
  2026-11-20T01:41:25-08:00; CMake and Ninja are not on `PATH`.
- **Freeze the four interfaces first**: `target.h`, `session.h`, `settings.h`,
  `surface.h`. They are the integration contract (`PLAN.md` §12 step 1), and a
  later change to any of them updates `PLAN.md` in the same change, before the
  code.
- Work on `main`. The ARM64 tester watches `main` and `adopt-krc` only, and the
  G0.3 row needs ARM64 evidence for the revisions you push; a side branch would
  not be tested. If you decide a branch is unavoidable, stop and ask first,
  because the tester protocol has to change with it.

## Scope

M0 only, in the order `implementation.md` §2 gives it: M0.1 CMake skeleton and
presets, M0.2 third-party and fonts, M0.3 Canvas, M0.4 platform windows, M0.5
fake target, M0.6 surface shell and headless baseline, M0.7 golden baseline,
M0.8 CI and gate scripts, M0.9 commands and environment record. Stop there.

Do not start M1. Do not edit `PLAN.md` except to keep an interface change true
first, and do not touch the parallel programme in `docs/requests/` — that is the
krc toolchain adoption, run by other sessions.

## Ground rules

- No build command exists yet. M0.1 creates the CMake project and presets; M0.9
  records the real configure/build/test/gate commands in `AGENTS.local.md`.
  Until then, do not claim a command exists, and do not tell anyone else to run
  one you have not run yourself.
- Warnings as errors. No new dependency without approval. Never install a
  compiler, SDK, CMake, Ninja or SDL: a missing tool is a report.
- The Windows deliverable must not require `SDL2.dll`; Linux needs SDL2.
  `PLAN.md` §4.8 is the chosen shape.
- Record every checked task with a one-line evidence note as `implementation.md`
  §0 requires, and put real command output in §9. Never check a box from memory,
  and never record expected output.
- The ARM64 row is the tester's `docs/arm64-test-log.md` entry for the tested
  revision. You do not run it and you do not wait for it; G0 may record that row
  `BLOCKED` until the tester has logged the revision.
- This repository's log is `implementation.md` itself: the evidence notes and
  §9. Do not add a separate log file.

## Gate

G0 in `implementation.md` §2, item by item, with its real output. A gate item is
checked only after the command ran and §9 holds the result. If a row fails, leave
the box empty and record why; a failing gate is a finding, not a formality.

## Stop and report

- the doctor fails, or a toolchain or SDK is missing;
- an interface cannot be frozen without a decision the plan does not make;
- a task needs something the plan does not decide (stop, do not invent a
  policy);
- a gate item depends on ARM64 and the tester has not logged that revision.

## Report back

The branch and HEAD SHA, which G0 items pass with their evidence, which are
blocked and why, and the next task from `implementation.md`.
