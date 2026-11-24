# CI and build trees

There is no hosted CI. Every gate runs on hardware this project owns, and each is a
script in `scripts/`:

| Platform | Runner | Gate |
|---|---|---|
| Windows x64 | this machine | `scripts/gate-windows.ps1` |
| Linux x64 | WSL | `scripts/gate-linux.sh` |
| Linux ARM64 | BitWhiz (Raspberry Pi, aarch64) | `scripts/gate-linux-arm64.sh` |
| Two adapters | this machine, both arms | `scripts/check-two-adapters.ps1` |

`gate-windows.ps1` and `gate-linux.sh` are the host matrix: each configures, builds
and runs `ctest --preset` for Debug and Release, so the unit binaries run as ctest
rows. `gate-linux.sh` takes `--arch x64|arm64` and, on ARM64, also records the frame
hashes it measured. The ARM64 gate is driven from the kr232 repository, where
`scripts/check-arm64.ps1` runs this repository's `gate-linux-arm64.sh` over SSH and
reports `GATE LINUX ARM64`.

Packaging is run by hand at a tag, not by a workflow:

| Target | Script |
|---|---|
| Windows | `scripts/package-windows.ps1` |
| Linux | `scripts/package-linux-tarball.sh` |

The ARM64 verification path is described in [ci-arm64.md](ci-arm64.md): a tester
session on BitWhiz polls `main` and runs the ARM64 gate, and its result is recorded
in [arm64-test-log.md](arm64-test-log.md).

Until 2026-11-23 this repository had a GitHub Actions workflow that re-ran the same
steps on hosted runners. It was removed: those runners cost minutes for a pipeline
whose toolchain nobody here chose, and the scripts above were always the gate.
