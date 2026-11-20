# CI and build trees

`.github/workflows/ci.yml` has three jobs:

| Job | Runner | Gate script |
|---|---|---|
| `windows-x64` | `windows-latest` | `scripts/gate-windows.ps1` |
| `linux-x64` | `ubuntu-latest` | `scripts/gate-linux.sh` |
| `arm64` | self-hosted `[self-hosted, linux, arm64, raspberry-pi]` | `scripts/gate-linux-arm64.sh` |

CI calls the gate scripts only; no command string is repeated in the workflow.
The ARM64 job runs only when the repository variable
`KRDBG_ARM64_RUNNER` is `true`; until a runner is registered it is skipped and
G0.9 records ARM64 as BLOCKED rather than leaving a job queued. The runner setup
is in [ci-arm64.md](ci-arm64.md).

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
