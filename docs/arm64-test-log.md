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

## 2026-11-20T08:52:46-08:00 — main @ da4d19b

- Host: BitWhiz
- Range: 121b9ab..da4d19b, 3 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; incremental, no source file changed since 121b9ab)
- Test: CTest — pass (2/2: `krdbg_unit` 25 checks, 0 failures, with the two x64 golden comparisons excluded; `platform_boundary` 13 files clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: the range is the CI workflow comment, the environment record and two G0.4 screenshots, so the build was incremental again and the 1 s wall time is not a clean-build time. Hashes are unchanged. `AGENTS.local.md` now records the krdbg runner registration as confirmed, so the `arm64` CI job and this tester target the same Pi; they use separate checkouts, but a queued CI build will compete with a tester build for CPU, so both can be slower than these timings.

## 2026-11-20T08:58:16-08:00 — main @ 503f5de

- Host: BitWhiz
- Range: da4d19b..503f5de, 2 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; the new M1 translation units compiled)
- Test: CTest — pass (2/2: `krdbg_unit` 36 checks, 0 failures, 34 passed with the two x64 golden comparisons excluded; `platform_boundary` 13 files clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M1.1 landed (`DebugInfo`, `merge_debug_info`, lookup and value resolution plus `test_lookup`, `test_merge`, `test_variables`), so `krdbg_unit` grew from 25 to 36 checks and this run took 36 s because the new translation units compiled. The UI is untouched, so the frame hashes are unchanged.

## 2026-11-20T09:08:08-08:00 — main @ 50b6a0d

- Host: BitWhiz
- Range: 503f5de..50b6a0d, 2 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; only the changed core and test translation units compiled)
- Test: CTest — pass (2/2: `krdbg_unit` 37 checks, 0 failures, 35 passed with the two x64 golden comparisons excluded; `platform_boundary` 13 files clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: range is `1dfd4c9` (empty source lookup and char formatting) plus `50b6a0d` (runner status), and the UI is untouched, so the frame hashes are unchanged. Runner check requested by the `AGENTS.local.md` trap: **no GitHub Actions runner is present on this host** — no `actions.runner.*` systemd unit, no `Runner.Listener` process, and no runner installation or `_work` directory under `/home`, `/opt`, `/srv` or `/var/lib`. That is consistent with the `arm64` jobs staying queued; the tester gate here is independent of CI and still runs.
