# KDBG fixtures

`fake.c` is compiled and linked with KRCompiler to produce:

- `fake.k32` — a KR32 image containing the `.kdbg` section;
- `fake.kdbg` — the raw `.kdbg` payload extracted from the image;
- `fake.krc.json` — `krc --dump-symbols --json` output, the parity golden.

Source path is recorded relative to the repository root (`--source-root`), so
the fixture is portable and deterministic. It is built with KRCompiler
`797dfacf7bbbc3e7744a40e627e7777a1c63b9c9`, `krc`/`krld` 0.1.0.

## Regenerate

From any directory, with built `krc` and `krld`:

```sh
python tests/fixtures/kdbg/regenerate.py \
    --krc /path/to/krc --krld /path/to/krld
```

The script compiles `fake.c`, links `fake.k32` with `--source-root=<repo>`,
writes the JSON dump, and extracts `fake.kdbg` from the KDBG magic to the end of
the image. Re-running it on the same compiler must produce byte-identical
fixtures; `tests/test_kdbg.cpp` decodes both forms and compares the translated
model with `fake.krc.json`.

The `.kdbg` payload is the last section of this image. If a newer compiler
places data after it, extend the extractor to use the container's section table
instead of the magic-to-EOF slice.
