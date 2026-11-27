# ARM64 verification

ARM64 is not a GitHub Actions job. There is no self-hosted runner and none is
planned.

The verification path is the family gate on BitWhiz:
`../kr232/scripts/check-arm64.ps1` fast-forwards the family checkouts, installs
the compiler into `~/krc-debug`, runs this repository's
`scripts/gate-linux-arm64.sh` and the kr232 fixture-drift row, and reports
`ARM64 CHECKS: PASS`. The gate run is the record; this repository keeps no ARM64
test log.

`scripts/gate-linux-arm64.sh` configures and builds the `linux-arm64` Debug and
Release presets, runs CTest, prints the ARM64 empty/loaded frame hashes and
records them in `build/gate/arm64-hashes.txt`. It never compares ARM64 against
the x64 hash.

ARM64 entries recorded before the family gate took over are in the git history of
`docs/arm64-test-log.md`.
