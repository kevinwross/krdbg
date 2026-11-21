# Licensing — the four repositories

**Status:** open. Recorded 2026-11-20T22:15:17-08:00.
**Applies to:** krdbg, KR32, kr16, KRCompiler. Every repository is MIT.
**Why this file exists:** the licensing work spans repositories and must ride on
each repository's next prompt, so it is written down rather than remembered.

## State

| Repository | Root `LICENSE` | Third-party notices | Remaining |
|---|---|---|---|
| KRCompiler | yes, MIT 2026 | n/a | nothing |
| kr16 | yes, MIT 2026 | n/a | nothing |
| krdbg | yes, MIT 2026 | `docs/licenses.md` complete | nothing |
| KR32 | **missing** | none yet | add root `LICENSE`; notices if the OS images carry third-party code |

The MIT text is identical in kr16 and KRCompiler — `MIT License`,
`Copyright (c) 2026 Kevin Ross` — and should be copied unchanged.

## Requirements

MIT requires the copyright line and permission notice in every copy, so each
repository gets a root `LICENSE` and each distributed artifact carries it. The
`docs/licenses.md` index is krdbg's third-party record; it stays the single home
for that list, and artifacts ship that file rather than a second copy.

The Linux x64 tarball bundles no SDL2, so `LICENSE`, `docs/licenses.md`, both
OFL texts and the `krc-debug` MIT notice are enough there. SDL2 2.32 is a
documented system requirement, not a bundled asset.

The OFL fonts and SDL2 keep their own licenses; MIT applies to these
repositories, not to those assets.

## Per-repository work for the next prompt

- **krdbg** (done): root `LICENSE` added; `LICENSE`, `docs/licenses.md`, both
  OFL texts and `third_party/krc-debug/LICENSE` ship in the Windows zip and
  Linux tarball; the scans assert them; MIT is recorded in `PLAN.md`. The
  tarball bundles no SDL2 and carries no SDL2 notice; SDL2 2.32 is a system
  requirement.
- **KR32** (with the release and 3.10b prep): add the root `LICENSE`; decide
  whether the shipped OS images carry third-party code and need a notice file;
  ship `LICENSE` with the release.
- **KRCompiler, kr16:** nothing to do; already MIT with a root `LICENSE`.
