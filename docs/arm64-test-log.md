# ARM64 test log — krdbg

Append-only. One entry per tested SHA, newest last. See `docs/arm64-tester.md` for
the protocol and `docs/ci-arm64.md` for the hash policy.

## 2026-11-20T08:36:36-08:00 — main @ 5fd5196

- Host: BitWhiz
- Range: first run (no previous entry; tip treated as untested)
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release, configure + build)
- Test: CTest — pass (2/2: `krdbg_unit` 25 checks, 0 failures, with the two x64 golden comparisons excluded; `platform_boundary` 13 files clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: ARM64 rasterization is byte-identical to the committed x64 goldens in both Debug and Release, so no `goldens/*.arm64.hash` file is needed for this revision. Gate observation, not fixed per §6: `scripts/gate-linux.sh:76` reuses `$preset` from the configure loop, so all four lines in `build/gate/arm64-hashes.txt` are labelled `linux-arm64-release`, including the pair from the Debug build. Toolchain: Debian g++ 14.2.0, CMake 3.31.6, Ninja 1.12.1, SDL2 2.32.4; gate wall time 43 s. No `SDL2.dll` found in the tree.

## 2026-11-20T08:42:55-08:00 — main @ 121b9ab

- Host: BitWhiz
- Range: 5fd5196..121b9ab, 3 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; incremental, no source file changed since 5fd5196)
- Test: CTest — pass (2/2: `krdbg_unit` 25 checks, 0 failures, with the two x64 golden comparisons excluded; `platform_boundary` 13 files clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: the previous entry's finding is fixed in `b605e5a` — the file is truncated per run and now holds exactly four correctly-labelled lines (two per preset). Hashes are unchanged from `5fd5196` and still identical to the x64 goldens. The build was incremental because only `scripts/` and Markdown changed, so the 1 s wall time is not a clean-build time. Toolchain unchanged: g++ 14.2.0, CMake 3.31.6, Ninja 1.12.1, SDL2 2.32.4.
