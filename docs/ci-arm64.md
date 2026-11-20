# ARM64 CI runner

The ARM64 gate runs on `BitWhiz`, a Raspberry Pi on the local network owned by
kevinwross. It polls GitHub every 7 minutes, so a queued job may wait that long
before it starts. The same machine is registered for KRCompiler; the krdbg
registration and its labels are confirmed by the owner in M0.8.

## Labels

```text
self-hosted, linux, arm64, raspberry-pi
```

`.github/workflows/ci.yml` selects exactly those labels. Do not add a label that
only one workflow uses without updating this file.

## Prerequisites on the Pi

Installed by the owner, not by CI:

- a C++20 compiler (`g++ --version` reports 12 or newer, or a C++20 clang);
- CMake 3.28 or newer and Ninja;
- `libsdl2-dev` (the Linux presenter links the system SDL2);
- `git` and the GitHub Actions runner.

A missing tool is a report, not an install performed by the agent or a gate
script. `scripts/gate-linux-arm64.sh` stops with the missing component named.

## Registration

From the repository's **Settings → Actions → Runners → New self-hosted
runner**, run the generated commands on the Pi with the labels above, for
example:

```sh
./config.sh --url https://github.com/kevinwross/krdbg \
    --token <registration-token> --name BitWhiz \
    --labels self-hosted,linux,arm64,raspberry-pi --work _work
sudo ./svc.sh install
sudo ./svc.sh start
```

The token is a one-time registration secret; it is never committed.

## Enabling the CI job

The `arm64` job is gated on a repository variable so CI does not leave a job
queued while no runner exists:

1. Register the runner above and confirm it is idle under **Settings → Actions →
   Runners**.
2. Set **Settings → Secrets and variables → Actions → Variables →
   `KRDBG_ARM64_RUNNER`** to `true`.
3. The next push runs the `arm64` job with the labels above. Until step 2, the
   job is skipped and G0.9 records ARM64 as BLOCKED for the missing runner.

## Gate and hash policy

The job runs `scripts/gate-linux-arm64.sh`, which configures and builds the
`linux-arm64` Debug and Release presets, runs CTest with the x64 golden tests
excluded, and prints the headless hash for the empty and loaded states.

ARM64 keeps its own rasterization hash. The gate compares against
`goldens/<state>.arm64.hash` when that file exists; otherwise it records the
hash in `build/gate/arm64-hashes.txt` and the ARM64 tester appends it to
`docs/arm64-test-log.md`. It never fails a build because ARM64 differs from the
x64 hash, and it never compares against another platform's result.
