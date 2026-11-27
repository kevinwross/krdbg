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

Latest release: `0.2.0`.

Status: release 0.2.0 completed M0–M6 with the KR32 and KR16 adapters, and
provided the Windows x64 zip, Linux x64 tarball and tester-built ARM64 tarball.
`main` adds the unreleased kr216 and kr232 adapters; [implementation.md](implementation.md)
records their current coverage.

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

With no arguments the debugger opens a startup chooser (OPEN IMAGE, OPEN
SOURCE, QUIT, and recent files from the session). `--load <source-or-image>`
builds a `.c`, `.cpp` or `.s` through the installed `krc`/`krld` package (set
`KRDBG_KRC_PREFIX` or pass `--toolchain <prefix>`), or opens a `.kr32`, `.kr16`,
`.kr216` or `.kr232` image directly. Source builds currently cover kr32, kr16 and
kr232; load a kr216 image with `--kr216` or `--load`. `--kernel` loads the
embedded KR32OS pair.

The Windows artifact is a single executable and contains no `SDL2.dll`. The
Linux x64 tarball requires SDL2 **2.32** from the system package manager and
bundles no SDL2. The ARM64 artifact is built by the tester on the Pi.

## Build

Configuring needs the pinned KRCompiler package on CMake's search path; the
presets do not add it themselves. Set `KRG_PREFIX` to the prefix that contains
`bin/krc` and pass it, or run `scripts/gate-windows.ps1`, which resolves the
prefix from `KRG_PREFIX`, `KRG_KRC`, or `../build/family-prefix`. The vendored
KDBG decoder does not remove the package requirement; see
[docs/toolchain.md](docs/toolchain.md).

```powershell
# Windows, from Developer PowerShell for VS 18:
cmake --preset windows-msvc-release "-DCMAKE_PREFIX_PATH=$env:KRG_PREFIX"
cmake --build --preset windows-msvc-release
```

```sh
# Linux x64 (WSL or Ubuntu):
cmake --preset linux-wsl-release "-DCMAKE_PREFIX_PATH=$KRG_PREFIX"
cmake --build --preset linux-wsl-release
```

The presets build with every VM adapter off. Configure an adapter with
`-DKRDBG_<VM>_ADAPTER=ON` and the matching `KRDBG_<VM>_ROOT`; see
[krdbg-newtarget.md](krdbg-newtarget.md).

For a working kr232 debugger use [docs/debugging-kr232.md](docs/debugging-kr232.md):
`scripts/build-debugger.ps1` (or `.sh`) builds the adapter-enabled binary, and
the page covers the fixtures, the kernel+user pair and the startrekng example.

The gate scripts in `scripts/` configure, build and test Debug and Release.
Release packaging is described in [docs/release.md](docs/release.md).
