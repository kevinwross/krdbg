# CI and build trees

There is no hosted CI. Every gate runs on hardware this project owns, and each is a
script in `scripts/`:

| Platform | Runner | Gate |
|---|---|---|
| Windows x64 | this machine | `scripts/gate-windows.ps1` |
| Linux x64 | WSL | `scripts/gate-linux.sh` |
| Linux ARM64 | BitWhiz (Raspberry Pi, aarch64) | `scripts/gate-linux-arm64.sh` |
| Adapter layout | this machine | `scripts/check-two-adapters.ps1` |

`gate-windows.ps1` and `gate-linux.sh` are the host matrix: each configures, builds
and runs `ctest --preset` for Debug and Release, so the unit binaries run as ctest
rows. `gate-linux.sh` takes `--arch x64|arm64` and, on ARM64, also records the frame
hashes it measured. The ARM64 gate is driven from the kr232 repository, where
`scripts/check-arm64.ps1` runs this repository's `gate-linux-arm64.sh` over SSH and
reports `GATE LINUX ARM64`.

`check-two-adapters.ps1` configures the kr216 and kr232 adapters and runs the
`krdbg_tests` binary to keep the render goldens independent of the linked
layout. On Windows it builds only `krdbg_tests`; the Linux gate builds the whole
two-adapter tree, including `krdbg_kr232_tests`.

Packaging is run by hand at a tag, not by a workflow:

| Target | Script |
|---|---|
| Windows | `scripts/package-windows.ps1` |
| Linux | `scripts/package-linux-tarball.sh` |

The ARM64 verification path is described in [ci-arm64.md](ci-arm64.md): the
family gate on BitWhiz runs this repository's ARM64 gate and reports the result.

Until 2026-11-23 this repository had a GitHub Actions workflow that re-ran the same
steps on hosted runners. It was removed: those runners cost minutes for a pipeline
whose toolchain nobody here chose, and the scripts above were always the gate.
