# krdbg

A target-agnostic, linked-in debugger surface for KR virtual machines. `krdbg`
provides one C++20 surface over a neutral session model, with per-VM adapters
and KDBG as the debug format.

- Design: [PLAN.md](PLAN.md)
- Implementation plan and gates: [implementation.md](implementation.md)
- Standing instructions: [AGENTS.md](AGENTS.md)
- Release layout: [docs/release.md](docs/release.md)
- Third-party licenses: [docs/licenses.md](docs/licenses.md)

Status: M0–M5 are complete. M6 packaging provides the Windows x64 release zip;
the Linux x64 AppImage awaits AppImage tooling; ARM64 packaging waits for the
tester machine.

## Run

Windows:

```powershell
krdbg_demo.exe
krdbg_demo.exe --headless --state loaded --screenshot loaded.ppm
```

Linux:

```sh
./krdbg_demo
./krdbg_demo --headless --state loaded --screenshot loaded.ppm
```

The debugger opens the fake target by default. `--load <source-or-image>`
builds a `.c`, `.cpp` or `.s` through the installed `krc`/`krld` package (set
`KRDBG_KRC_PREFIX` or pass `--toolchain <prefix>`), or opens a `.k32`/`.kex`
image directly. `--kernel` loads the embedded KR32OS pair.

The Windows artifact is a single executable and contains no `SDL2.dll`. The
Linux AppImage will bundle SDL2; the ARM64 artifact is built by the tester on the
Pi.

## Build

```sh
# Windows, from a Visual Studio developer shell:
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release

# Linux x64 (WSL or Ubuntu):
cmake --preset linux-wsl-release
cmake --build --preset linux-wsl-release
```

The gate scripts in `scripts/` configure, build and test Debug and Release.
Release packaging is described in [docs/release.md](docs/release.md).
