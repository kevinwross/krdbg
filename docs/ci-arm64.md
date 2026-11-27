# ARM64 verification

ARM64 is not a GitHub Actions job. There is no self-hosted runner and none is
planned.

The verification path is the family gate on BitWhiz:
`../kr232/scripts/check-arm64.ps1` invokes `scripts/gate-linux-arm64.sh` over SSH
and reports `GATE LINUX ARM64`. The protocol for a direct tester session is in
[docs/arm64-tester.md](arm64-tester.md); that file is authoritative and is not
duplicated here.

`docs/arm64-test-log.md` records runs made directly from this checkout. The
latest entry for a revision is its ARM64 evidence.
