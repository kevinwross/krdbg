# krdbg

A target-agnostic, linked-in debugger surface for KR virtual machines. `krdbg`
provides one C++20 surface over a neutral session model, with per-VM adapters
and KDBG as the debug format.

- Design: [PLAN.md](PLAN.md)
- Implementation plan and gates: [implementation.md](implementation.md)
- Standing instructions: [AGENTS.md](AGENTS.md)

Status: M0 is implemented — the CMake project and presets, portable canvas,
Win32 and SDL2 presenters, fake target, surface shell, golden baseline and gate
scripts. G0 remains open on ARM64 evidence and the WSL window check; M1 is the
next milestone. Build commands are in `AGENTS.local.md`.
