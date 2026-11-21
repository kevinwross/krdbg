# ARM64 test log — krdbg

Append-only. One entry per tested SHA, newest last. See `docs/arm64-tester.md` for
the protocol; `docs/ci-arm64.md` records that ARM64 is tester-verified, not a CI
job.

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

## 2026-11-20T09:23:22-08:00 — main @ 649735b

- Host: BitWhiz
- Range: 50b6a0d..649735b, 4 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; the new `krdbg_kdbg` library compiled both the vendored decoder and the adapter)
- Test: CTest — pass (2/2: `krdbg_unit` 42 checks, 0 failures, 40 passed with the two x64 golden comparisons excluded; `platform_boundary` 14 files clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M1.2 and M1.5 landed — `src/kdbg/kdbg.cpp` and `include/krdbg/kdbg.h` over the vendored `third_party/krc-debug/` pair (KRCompiler `797dfacf` per `AGENTS.local.md`), with the `fake.k32` (4671 B) / `fake.kdbg` (1455 B) fixtures and `test_kdbg`. `krdbg_unit` grew from 37 to 42 checks. The boundary check now covers 14 files instead of 13 because `include/krdbg/kdbg.h` joined the public set, and it is clean. The UI is untouched, so the frame hashes are unchanged. 41 s because the two new KDBG translation units compiled.

## 2026-11-20T09:28:28-08:00 — main @ ba568d0

- Host: BitWhiz
- Range: 649735b..ba568d0, 2 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; the session engine, fake target and the new session tests compiled)
- Test: CTest — pass (2/2: `krdbg_unit` 50 checks, 0 failures, 48 passed with the two x64 golden comparisons excluded; `platform_boundary` 14 UI/public files clean and 10 core/kdbg files VM-clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M1.3–M1.4 landed; `krdbg_unit` grew from 42 to 50 checks and takes 1.16 s in Debug now that the session tests run. `include/krdbg/session.h` gained an optional `Clock` test seam, and `PLAN.md` §4.5 documents it in the same commit, so the interface rule holds. The boundary check gained a second assertion — `10 core/kdbg files VM-clean` — and both pass. The UI is untouched, so the frame hashes are unchanged.

## 2026-11-20T09:31:48-08:00 — main @ 1c978f6

- Host: BitWhiz
- Range: ba568d0..1c978f6, 3 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; `session.cpp` and the surface/test link set rebuilt)
- Test: CTest — pass (2/2: `krdbg_unit` 50 checks, 0 failures, 48 passed with the two x64 golden comparisons excluded; `platform_boundary` 14 UI/public files clean and 10 core/kdbg files VM-clean)
- Hash: empty `F15B00C153682DF8`, loaded `E47FA2651086D075` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: `2aee61f` makes `replace_target` refresh the debug info together with the new target and extends the existing `replace_target` test to re-resolve a moved line breakpoint, so the check count stays at 50; `1c978f6` marks M1/G1 done using this run as the ARM64 evidence. The UI is untouched, so the frame hashes are unchanged.

## 2026-11-20T09:53:15-08:00 — main @ 1c80f16

- Host: BitWhiz
- Range: 1c978f6..1c80f16, 3 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; the surface, scroll and SDL2 presenter translation units rebuilt)
- Test: CTest — pass (2/2: `krdbg_unit` 62 checks, 0 failures, 60 passed with the two x64 golden comparisons excluded; `platform_boundary` 15 UI/public files clean and 10 core/kdbg files VM-clean)
- Hash: empty `5B8F7CFA1DDEE5D2`, loaded `BEFD2141466934DD` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M2.1 is the first UI change since M0, so the hashes moved from `F15B00C153682DF8` / `E47FA2651086D075` to the new values; ARM64 matches the regenerated x64 goldens exactly in Debug and Release, so `goldens/*.arm64.hash` is still unnecessary. `krdbg_unit` grew from 50 to 62 checks. The boundary check now covers 15 UI/public files because `src/ui/scroll.h` joined the set. Caveat: the gate renders and hashes only the empty and loaded states, so the new `goldens/running.hash` has no ARM64 evidence — the running state is exercised by the x64 golden test, which the ARM64 unit run excludes.

## 2026-11-20T10:19:12-08:00 — main @ c41e0de

- Host: BitWhiz
- Range: 1c80f16..c41e0de, 5 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; the surface, settings JSON, platform and video code all compiled, 91 s)
- Test: CTest — pass (2/2: `krdbg_unit` 85 checks, 0 failures, 83 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 12 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `A2F1B7AEB69EB1A5` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M2.2–M2.7 landed — ~2,700 lines of surface plus `settings_json` and `video_fit` — and both hashes match the regenerated x64 goldens exactly in Debug and Release. `krdbg_unit` grew from 62 to 85 checks. The boundary check now covers 16 UI/public files (`src/ui/video_fit.h` added) and 12 core/kdbg files (`src/core/settings_json.*` added). Same caveat as before, now larger: the gate renders and hashes only empty and loaded, so `goldens/running.hash` (`36C15F50552FF36A`) and the new `goldens/video.hash` (`45C97E0E11204576`) have no ARM64 evidence; both are checked only by the x64 golden test.

## 2026-11-20T10:28:51-08:00 — main @ ad4f8dc

- Host: BitWhiz
- Range: c41e0de..ad4f8dc, 2 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; surface, fake target and settings rebuilt, 72 s)
- Test: CTest — pass (2/2: `krdbg_unit` 99 checks, 0 failures, 97 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 12 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M2 panel gaps plus M2.8/M2.9 coverage; the loaded state changed and the empty state did not, and both match the regenerated x64 goldens exactly. `include/krdbg/target.h` gained `IVideoSource::framebuffer_base()` with a default, documented in `PLAN.md` in the same commit, so the interface rule holds. `krdbg_unit` grew from 85 to 99 checks. Golden coverage is now 13 states, and the gate still renders only empty and loaded, so **11 of 13 goldens** (`com`, `context`, `error`, `no-com`, `no-video`, `running`, `settings`, `tooltip`, `variables`, `video`, `video16`) have no ARM64 evidence at all.

## 2026-11-20T11:10:51-08:00 — main @ 3e0d44e

- Host: BitWhiz
- Range: ad4f8dc..3e0d44e, 3 commits
- Docs-only: yes — logged at the owner's request despite the skip rule
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; clean build with both preset trees removed first, 116 s cold)
- Test: CTest — pass (2/2: `krdbg_unit` 99 checks, 0 failures, 97 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 12 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: deliberate duplicate of the `ad4f8dc` entry, requested by the owner to record a cold build. Both preset build trees were deleted before the run; the cold build took 116 s against 72–91 s for the warm incremental runs, and produced byte-identical hashes, so nothing depended on stale objects. `5cfd5d1` landed after the build and is docs-only and source-identical to `3e0d44e`, which is the revision actually built here.

## 2026-11-20T11:49:04-08:00 — main @ 9d63589

- Host: BitWhiz
- Range: 3e0d44e..9d63589, 6 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; core, KDBG and test translation units rebuilt, 76 s)
- Test: CTest — pass (2/2: `krdbg_unit` 99 checks, 0 failures, 97 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 12 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M3 added `adapters/kr32`, `tests/test_kr32_adapter.cpp` and `goldens/kr32-loaded.hash`, but `KRDBG_KR32_ADAPTER` is **OFF** by default and the gate does not enable it, so **this entry covers the core changes only** — the adapter, its test and the KR32 golden have no ARM64 evidence, and `krdbg_unit` stays at 99 checks because `test_kr32_adapter.cpp` is only added when the adapter is on. Two facts for anyone enabling it here: the default `KRDBG_KR32_ROOT` is `../KR32`, which does not match this host's lowercase `../kr32` on a case-sensitive filesystem, so `-DKRDBG_KR32_ROOT` must be passed explicitly; and this host has a `kr32` checkout at `/home/kevinro/repos/kr32` whose revision is not pinned by krdbg.

## 2026-11-20T11:54:14-08:00 — main @ 7539b23

- Host: BitWhiz
- Range: 9d63589..7539b23, 3 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (2/2 suites, 4 s: the gate rebuilt nothing because the changed file is not in its build)
- Test: gate `krdbg_unit` 99 checks, 0 failures, x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 12 core/kdbg files VM-clean
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Extra check, not the gate: configured `-DKRDBG_KR32_ADAPTER=ON -DKRDBG_KR32_ROOT=/home/kevinro/repos/kr32 -DKRDBG_PLATFORM=null -DCMAKE_BUILD_TYPE=Debug` in `build/adapter-check`, then built and ran it. **Build OK, 106 checks, 0 failures**, including the eight `kr32_*` cases. The changed file `tests/test_kr32_adapter.cpp` is compiled there, and `kr32_adapter_os_mode_loads_embedded_images_and_video` — the case that gained the OS serial-input assertions — passes. `kr32_ui_loaded_state_matches_the_committed_golden` also passes, so `goldens/kr32-loaded.hash` (`19D243A88CB8EF35`) is confirmed on ARM64, which the gate never checks.
- Notes: the sibling KR32 tree used for the extra check is at `aa120ad`, which krdbg does not pin, and the check uses the null presenter; it is evidence for this host, not a reproducible gate row. The gate's own 4 s run proves nothing about the adapter test change, which is why the extra build was added.

## 2026-11-20T13:01:18-08:00 — main @ dee054e

- Host: BitWhiz
- Range: 7539b23..dee054e, 5 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; 71 s)
- Test: gate CTest — pass (2/2: `krdbg_unit` 99 checks, 0 failures, 97 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 12 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Extra check, not the gate: configured `-DKRDBG_KR16_ADAPTER=ON -DKRDBG_PLATFORM=null -DCMAKE_BUILD_TYPE=Debug` in `build/kr16-check`, which used the default root `../kr16` (lowercase, so it resolves on this host), then built and ran it. **Build OK, 107 checks, 0 failures**, including the eight `kr16_*` cases, and `kr16_ui_loaded_state_matches_the_committed_golden` passes, so `goldens/kr16-loaded.hash` (`A1F5B45032915A96`) is confirmed on ARM64. The KR16 adapter is OFF in the gate, so none of this is gate coverage.
- Notes: M4 added `adapters/kr16`, `tests/test_kr16_adapter.cpp`, the `.kex`/legacy fixtures and the sixteenth-shape golden; goldens are now 15 files and the gate still renders only empty and loaded. The KR16 tree used for the extra check is at `b9f7436`, which krdbg does not pin, and the check uses the null presenter. The KR32 adapter was not enabled in this run, so `adapters/kr32` was not re-verified at this SHA.

## 2026-11-20T13:58:08-08:00 — main @ 3b506d5

- Host: BitWhiz
- Range: dee054e..3b506d5, 5 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; the new settings host, tool settings, their tests and the `krdbg_settings` CLI compiled, 26 s)
- Test: CTest — pass (2/2: `krdbg_unit` 105 checks, 0 failures, 103 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 17 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: M5.2/M5.4/M5.5 added `src/core/settings_host.*`, `tool_settings.*`, `settings_validation.h`, `apps/settings_cli` and `test_settings_host`; `krdbg_unit` grew from 99 to 105 checks and the boundary check now covers 17 core/kdbg files. `krdbg_settings` builds on ARM64 as a plain target. A sixteenth golden, `goldens/tool-settings.hash` (`3396C255BD1081B6`), has been added but the gate still renders only empty and loaded, so it has no ARM64 evidence, like the other twelve non-gate goldens.

## 2026-11-20T14:30:14-08:00 — main @ 81e1c31

- Host: BitWhiz
- Range: 3b506d5..81e1c31, 6 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; `src/host/build_chain.*` and `process.*` compiled into `krdbg_host`, 76 s)
- Test: CTest — pass (2/2: `krdbg_unit` 105 checks, 0 failures, 103 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 17 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Extra check, not the gate: rebuilt `build/kr16-check` (`KRDBG_KR16_ADAPTER=ON`, null presenter) at this SHA. **Build OK, 115 checks, 0 failures** — the default 105 plus the eight `kr16_*` cases and the two `host_build` cases.
- Notes: the host build chain's end-to-end tests are conditional twice over, and neither condition holds here — `tests/test_host_build.cpp` is only compiled when an adapter is enabled, and its cases `return` immediately when `KRDBG_KRC_PREFIX` is unset. So `host_build_chain_builds_c_cpp_and_s` and `host_rebuild_preserves_line_breakpoints` report PASS without asserting anything on this host, and `krc`/`krld` are not installed here. M5's build chain therefore has **no ARM64 end-to-end evidence**; only that it compiles. Goldens stay at 16, and the gate still renders only empty and loaded.

## 2026-11-20T14:42:44-08:00 — main @ 348e0a2

- Host: BitWhiz
- Range: 81e1c31..348e0a2, 2 commits
- Docs-only: no (`85f6221` changes `.github/workflows/ci.yml` as well as Markdown)
- Build: `scripts/gate-linux-arm64.sh` — pass (4 s, no rebuild: the only non-Markdown file is the workflow)
- Test: CTest — pass (2/2: `krdbg_unit` 105 checks, 0 failures, 103 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 17 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: `85f6221` removes the `arm64` GitHub Actions job and names this tester log as the only ARM64 verification path, which `AGENTS.local.md` and `docs/ci-arm64.md` now say. No source, test or adapter file changed since `81e1c31`, so the 105-check result and the code-compiles evidence carry over unchanged; this entry records the workflow change and the fact that the gate had nothing to compile (4 s, not a build claim).

## 2026-11-20T22:26:39-08:00 — main @ 1e6ea28

- Host: BitWhiz
- Range: 348e0a2..1e6ea28, 7 commits
- Docs-only: no
- Build: `scripts/gate-linux-arm64.sh` — pass (Debug and Release; the vendored KDBG translator and tests recompiled, 26 s)
- Test: CTest — pass (2/2: `krdbg_unit` 105 checks, 0 failures, 103 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 17 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`)
- Notes: KDBG v1.1 is vendored from KRCompiler `e04b3b2` — `third_party/krc-debug/` and `src/kdbg/kdbg.cpp` were replaced and the `.k32`/`.kdbg` fixtures regenerated. The five `kdbg_*` cases all pass, including `kdbg_rejects_truncated_bad_crc_and_bad_version` and `kdbg_translation_matches_krc_json_for_every_carried_field`, so the v1.1 pair decodes the new fixtures and keeps the `krc --dump-symbols --json` parity on ARM64. The check count stays at 105. `scripts/package-windows.ps1` is Windows-only and was not exercised here; M6.2 (AppImage) remains blocked per `AGENTS.local.md`.

## 2026-11-20T22:32:35-08:00 — main @ 116806d

- Host: BitWhiz
- Range: 1e6ea28..116806d, 3 commits
- Docs-only: no (release 0.2.0: `CMakeLists.txt`, `include/krdbg/version.h`, `LICENSE`, packaging scripts)
- Build: `scripts/gate-linux-arm64.sh` — pass (4 s, no rebuild: no compiled translation unit changed)
- Test: CTest — pass (2/2: `krdbg_unit` 105 checks, 0 failures, 103 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 17 core/kdbg files VM-clean)
- Hash: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` (recorded in `build/gate/arm64-hashes.txt`) — unchanged, and correctly so
- Notes: this is the 0.2.0 release commit (CMake `project(VERSION 0.2.0)`, MIT `LICENSE`, `scripts/package-linux-tarball.sh`, Windows packaging). The goldens did not move because **`include/krdbg/version.h` is referenced by nothing**: `kVersionString`, `kVersionMajor`, `kVersionMinor`, `kVersionPatch` and `kInterfaceVersion` have no consumer in `src`, `apps`, `tests` or `adapters`, which is also why the gate rebuilt nothing in 4 s. The 0.2.0 binary therefore carries no runtime version and there is no `--version`; the only 0.2.0 markers are the CMake project version and the package filenames. Reported, not fixed.

## 2026-11-20T22:48:28-08:00 — release 0.2.0 @ 68e56c6 (M6.3 ARM64 package)

- Host: BitWhiz
- Range: `68e56c6`, the release commit the Windows zip and Linux x64 tarball were built from — checked out detached into a clean directory (`build/m6.3/checkout`, `git status` empty), **not** main's tip
- Docs-only: n/a — this is the M6.3 package build, not a poll of a moving branch
- Build: clean worktree; `scripts/gate-linux-arm64.sh` — pass, Debug and Release, 173 s (87 steps per preset)
- Test: CTest — pass (2/2: `krdbg_unit` 105 checks, 0 failures, 103 passed with the two x64 golden comparisons excluded; `platform_boundary` 16 UI/public files clean and 17 core/kdbg files VM-clean)
- Hash (gate, run at the worktree root): empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2` — both match the committed x64 goldens
- Package: `krdbg-0.2.0-linux-arm64.tar.gz`, 371 240 bytes, SHA-256 `2ac4d44acf8cf681a707ce6498894e64922aaff58b24f1e236b2ce2cc0439a1c`
- Smoke: extracted outside the build tree (`build/m6.3/smoke`), `sha256sum -c SHA256SUMS` all OK, `./krdbg_demo --headless --state loaded --screenshot loaded.ppm` → **exit 0**, frame `DE4BCC6064D8B4F7 1440x900`; binary is `ELF 64-bit LSB pie executable, ARM aarch64`
- Notes: `scripts/package-linux-tarball.sh` has no `--arch` option and hardcodes `linux-x64` in the artifact name, `SYSTEM-REQUIREMENTS.txt`, `VERIFY.txt` and its `Build:` line. I ran it with `--build-dir build/linux-arm64-release --version 0.2.0 --commit 68e56c6`, then, from that same staged tree, renamed the directory to `krdbg-0.2.0-linux-arm64`, corrected those two generated labels to arm64 (`Build: linux-arm64-release`), regenerated `SHA256SUMS` with the script's own loop, and re-tarred with the script's exact flags (`--sort=name --owner=0 --group=0 --numeric-owner`). krdbg will add `--arch`; this workaround is reported here rather than left implicit. The script's x64-named tarball was deleted so it cannot be mistaken for the x64 artifact. The packaged smoke hash is **not** an ARM64 divergence: the loaded state resolves `examples/fake/main.c` relative to the working directory — with the source present it renders `3DAD6F9400E135E2` (the committed golden), without it `DE4BCC6064D8B4F7`, and the packaging script runs from the staging directory, so `VERIFY.txt` records `DE4BCC…` as its own documented behaviour. This also means the golden is only reproducible from a directory containing `examples/fake/main.c`; the gate runs at the repo root, which is why it still matches.
- Artifact path on the Pi: `/home/kevinro/repos/krdbg/build/m6.3/checkout/build/release/krdbg-0.2.0-linux-arm64.tar.gz`
