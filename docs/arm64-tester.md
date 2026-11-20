# ARM64 tester instructions — krdbg

**Created:** 2026-11-20T03:19:09-08:00
**Read by:** the ARM64 tester session on a Raspberry Pi.
**Repository:** `https://github.com/kevinwross/krdbg.git` — the clone on the Pi.
**Watch:** `main` only; this repository has no `adopt-krc` branch.
**You are:** the single ARM64 tester for this repository.

---

## 1. What you do

You poll the remote. When a watched branch gains a pushed commit that is more than
documentation, you build it, run its tests on ARM64, append one entry to
`docs/arm64-test-log.md`, commit that entry and push it. Then poll again.

You never build work that exists only on another machine. The remote is the
source of truth: if `origin` does not advertise it, it does not exist for you.

## 2. Polling

Every 5 minutes, and immediately after finishing a job:

```sh
git fetch --prune origin
```

Compare `git rev-parse --short origin/main` with the last tested SHA (§5). If it
has not moved, do nothing.

The checkout must be clean apart from the log entry you are about to add:

```sh
git status --porcelain
```

If anything else is modified or untracked, stop and report.

## 3. Is it more than documentation?

```sh
git diff --name-only <last-tested-sha>..origin/main
```

Skip when every path is a Markdown file (`*.md`), under `docs/`, or under
`.agents/`. Do not log a skipped push; the next real build's range covers it.

If the last tested SHA is no longer an ancestor of the new tip (a rebase or
force-push), test the tip and write `history rewritten` in the entry.

## 4. Build and test

**There is no build system yet.** Until the repository contains
`CMakePresets.json` (milestone M0.1), the only correct action on a
non-documentation push is:

- append one entry saying `No build system: M0.1 has not landed`;
- stop.

Do not invent a configure command. Do not create a build directory.

When the build system lands, its entry point is the "Build entry point" row in
`AGENTS.local.md`, and the ARM64 gate is `scripts/gate-linux-arm64.sh` (M0.8.3).
Follow those; this section is then superseded. Until then, the repository is
documentation only — the plans in `docs/requests/` and the top-level files — and
there is nothing to build.

Record how long each step took. This machine is slower than the developer's; a
long runtime is expected, not a finding.

## 5. The log

Append to `docs/arm64-test-log.md` on `main`. One entry per tested SHA:

```markdown
## 2026-11-20T03:19:09-08:00 — main @ 6d638c4

- Host: <hostname>
- Range: 3bc3942..6d638c4, 4 commits
- Docs-only: no
- Build: none — no build system (M0.1 not landed)
- Test: not run
- Skipped: everything; nothing to build
- Next: watch `main` for the M0.1 CMake skeleton
```

Rules:

- Call `local-datetime` immediately before writing the entry and copy its `iso`
  value verbatim into the heading.
- `<hostname>` is this machine, not `BitWhiz`: other Raspberry Pis test other
  repositories, and the log must say which one ran.
- Append at the end. Never rewrite an earlier entry; correct it with a new one.
- Commit and push it after the run:

```sh
git add docs/arm64-test-log.md
git commit -m "Log ARM64 test of main @ <short-sha>"
git pull --rebase
git push
```

If the log conflicts during the rebase, keep both entries, newest last. If the
push is rejected twice, stop and report. The log commit is documentation-only,
so it does not trigger another build.

## 6. What you are not responsible for

- **x64 results.** Windows and WSL build on the developer's machine and finish
  later; the gap is expected. Report ARM64 only. Never wait for, compare against,
  or fail a commit because another platform has not run.
- **Fixing the code.** A failure is a report, not an edit. Do not touch source,
  do not disable a test, do not retry for a different result.
- **Installing tools.** A missing compiler, CMake, Ninja or SDL is a report.

## 7. Stop and report

Stop rather than guess when: the checkout is dirty; a preset is missing; a
toolchain is absent; configure fails; the build fails; the tests fail; the log
cannot be pushed after one rebase and retry.

A report is: the branch, the SHA, the exact command, the real output, and what
you did not run.

## 8. State

Your last tested SHA is the most recent `docs/arm64-test-log.md` entry for the
branch. With no entry, treat the tip as untested and write `first run`.
