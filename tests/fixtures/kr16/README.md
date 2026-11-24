# KR16 M4 fixtures

`m4.kr16` is a deterministic 2,620-byte KR16 executable built by `krc` from
KRCompiler at `0defcce` or later. It carries `.kdbg` and is the primary M4
fixture.

- Source: `m4.c`
- KDBG target: 2 (KR16); functions `add` and `main`; locals `point`, `total`,
  `input`; global `global_counter`
- SHA-256: `D2709C451D4736CBEEFE42D50D2CE65A2EEF45351473AD78AA50CA9008938D58`
- Rebuild: `tests/fixtures/kr16/regenerate.ps1`

`legacy.kr16` is a 1,954-byte image from KR16's built-in `cc -g -O0` + `ld`.
It carries `.debug_line`/`.debug_vars`/`.debug_types` and no `.kdbg`, so it
exercises the legacy fallback for older images.

- SHA-256: `DE35D84DB99BA95012F05F32206ED312B35A24CF86D650B890E832FCE1B132D0`
- Rebuild: `kr16 cc -g -O0 tests/fixtures/kr16/m4.c -o build/m4/legacy.o` then
  `kr16 ld build/m4/legacy.o -o tests/fixtures/kr16/legacy.kr16`

Both images are small and deterministic, so they are checked in. Run the
script from the repository root. Set `KRDBG_KRC` to the `krc` executable and
`KRDBG_KR16` to the `kr16` executable to rebuild both.
