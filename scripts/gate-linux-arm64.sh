#!/usr/bin/env bash
# Gate G0 on the ARM64 Raspberry Pi. The shared Linux gate owns the commands;
# this wrapper selects the ARM64 presets and the separate-hash policy.
set -euo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/gate-linux.sh" --arch arm64
