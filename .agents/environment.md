<!--
  Delivered by DevTemplate 0.7.2 on 2026-11-18. The project owns this file
  from here; edit it freely. Refresh the platform sections only with:
    node scripts/sync.mjs --update --refresh-env

  The rules live in AGENTS.md: discover, never substitute, never install. This file is the
  how, per platform. Read it when the doctor command fails or a build cannot find its
  compiler, and add what you learn to the Environment section of AGENTS.local.md.
-->
# Build environment — krdbg

## When a build cannot find its compiler

1. Run the doctor command recorded in `AGENTS.local.md`.
2. Quote the failing command and its output, and name the missing component in plain terms
   ("the MSVC C++ build tools are not installed").
3. Stop there. Installing, vendoring, and substituting are decisions the user makes, not
   repairs an agent performs to turn a build green.

## The three anti-patterns this file exists to prevent

- **Substituting a compiler.** A build that passes under a compiler the project does not
  target has not reproduced the project's build. Warning levels, headers, and the C
  runtime belong to the toolchain; success under another one proves nothing.
- **Vendoring a toolchain.** A compiler, SDK, or build tool downloaded into the repository
  is a permanent supply-chain surface and a dependency nobody declared.
- **Repairing `PATH`.** Adding a compiler directory to `PATH` from a project script
  encodes one machine's layout into a file every machine runs.

## Windows: locating MSVC

`cl.exe` is not on `PATH` in an ordinary PowerShell, cmd, or git-bash session, so a failed
`where cl` says nothing about whether Visual Studio is installed.

Query the installer's index instead:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
```

- **Empty output**: the C++ workload is genuinely absent. Report that and stop.
- **A path**: MSVC is present. The toolchain is under `VC\Tools\MSVC\<version>\bin\Hostx64\x64`.

Use the compiler one of two ways:

- Let CMake find it: `cmake -S . -B build -G <generator>`. Run `cmake -G` with no argument to
  list the generators this CMake knows; the generator name tracks the Visual Studio version
  and is easy to misremember.
- Initialise a shell explicitly with `VC\Auxiliary\Build\VsDevCmd.bat -arch=x64 -host_arch=x64`,
  after which `cl` and `msbuild` resolve.

The Windows SDK is separate from the compiler and independently missing. Check for
`%ProgramFiles(x86)%\Windows Kits\10\Include` and name the SDK component if it is absent.

A `cmd /c` call or a git-bash shell inherits the caller's environment; neither gains `cl`
by being launched from a different shell.

## Linux: discovering the toolchain

```sh
command -v cc gcc clang cmake ninja pkg-config
cc --version
pkg-config --modversion <package>
```

`cc` is usually a symlink to the distribution's default compiler; read what it resolves to
rather than assuming gcc or clang. A missing `cmake` or `ninja` is a package-manager
install and therefore an approval, not a build step.

## Linux on ARM64: what differs

`uname -m` should report `aarch64`. The failures here are usually not missing compilers but
x64 artifacts: a prebuilt binary that resolves for x64 alone silently falls back to building
from source, or produces something that cannot load.

Check that every native dependency has a prebuilt artifact for ARM64 or builds natively on
the device. Cross-compilation is a separate decision with its own sysroot, and adopting it
to get a build working changes the target the project tests.

## Recording what you found

Put the result in the Environment section of `AGENTS.local.md`: compiler and version, the
path it resolved to, the machine, and the date. Re-verify after a toolchain change.

A record you did not verify is worse than no record, because the next session believes it
and stops looking.
