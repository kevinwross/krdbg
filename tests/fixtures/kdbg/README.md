# KDBG fixtures

`fake.c` is compiled and linked with KRCompiler to produce:

- `fake.kr32` — a KR32 image containing the `.kdbg` section;
- `fake.kdbg` — the raw `.kdbg` payload extracted from the image;
- `fake.krc.json` — `krc --dump-symbols --json` output, the parity golden.

Source path is recorded relative to the repository root (`--source-root`), so
the fixture is portable and deterministic. It is built with KRCompiler
`e04b3b27541d62d98d6779e66f4508c77e91a1f6`, KDBG v1.1, `krc`/`krld` 0.1.0.

## Regenerate

From any directory, with built `krc` and `krld`:

```sh
python tests/fixtures/kdbg/regenerate.py \
    --krc /path/to/krc --krld /path/to/krld
```

The script compiles `fake.c`, links `fake.kr32` with `--source-root=<repo>`,
writes the JSON dump, and extracts `fake.kdbg` from the KDBG magic to the end of
the image. Re-running it on the same compiler must produce byte-identical
fixtures; `tests/test_kdbg.cpp` decodes both forms and compares the translated
model with `fake.krc.json`.

The `.kdbg` payload is the last section of this image. If a newer compiler
places data after it, extend the extractor to use the container's section table
instead of the magic-to-EOF slice.
