# ARM64 verification

ARM64 is not a GitHub Actions job. There is no self-hosted runner and none is
planned.

The verification path is the ARM64 tester session on BitWhiz. It polls `main`,
runs `scripts/gate-linux-arm64.sh` and appends one entry to
`docs/arm64-test-log.md`. The protocol is in
[docs/arm64-tester.md](arm64-tester.md); that file is authoritative and is not
duplicated here.

The latest entry in `docs/arm64-test-log.md` is the ARM64 evidence for its
revision. CI keeps only the Windows and Ubuntu jobs in
`.github/workflows/ci.yml`.
