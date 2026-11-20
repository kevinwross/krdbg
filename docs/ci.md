# CI and build trees

`.github/workflows/ci.yml` has two jobs:

| Job | Runner | Gate script |
|---|---|---|
| `windows-x64` | `windows-latest` | `scripts/gate-windows.ps1` |
| `linux-x64` | `ubuntu-latest` | `scripts/gate-linux.sh` |

CI calls the gate scripts only; no command string is repeated in the workflow.
ARM64 is not a GitHub Actions job: the ARM64 tester session on BitWhiz runs
`scripts/gate-linux-arm64.sh` and appends `docs/arm64-test-log.md`; see
[ci-arm64.md](ci-arm64.md) and [arm64-tester.md](arm64-tester.md).

## Build trees and artifacts

- Build trees live under `build/` and are ignored by Git; no configure output,
  object file, executable, PPM or test log is committed.
- M0 uses no CI cache: a cold build on each runner keeps the toolchain and the
  golden hash honest. A cache may be added when build time warrants it, but
  never for `goldens/`.
- M0 uploads no artifacts. Test logs and goldens are uploaded from M2
  (`implementation.md` M2.9.3); secrets never enter either.
- The only committed generated file is `src/ui/embedded_fonts.cpp`, produced by
  `third_party/fonts/embed_fonts.py` from the committed subset fonts.
