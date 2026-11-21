# krdbg

A target-agnostic, linked-in debugger surface for KR virtual machines. `krdbg`
provides one C++20 surface over a neutral session model, with per-VM adapters
and KDBG as the debug format.

- Design: <https://github.com/kevinwross/krdbg/blob/main/PLAN.md>
- Implementation plan and gates: <https://github.com/kevinwross/krdbg/blob/main/implementation.md>
- Standing instructions: <https://github.com/kevinwross/krdbg/blob/main/AGENTS.md>
- Release layout: [docs/release.md](docs/release.md)
- Third-party licenses: [docs/licenses.md](docs/licenses.md)
- License: [LICENSE](LICENSE) (MIT)

Version: `0.2.0`.

Status: M0–M5 are complete. M6 packaging provides the Windows x64 zip and the
Linux x64 tarball; ARM64 packaging waits for the tester machine.

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
Linux x64 tarball requires SDL2 **2.32** from the system package manager and
bundles no SDL2. The ARM64 artifact is built by the tester on the Pi.

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
