# krdbg

A target-agnostic, linked-in debugger surface for KR virtual machines. `krdbg`
provides one C++20 surface over a neutral session model, with per-VM adapters
and KDBG as the debug format.

- Design: [PLAN.md](PLAN.md)
- Implementation plan and gates: [implementation.md](implementation.md)
- Standing instructions: [AGENTS.md](AGENTS.md)

Status: pre-implementation. No build system exists yet; M0 in
`implementation.md` creates it.
