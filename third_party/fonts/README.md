# Embedded UI fonts

The canvas embeds subsetted glyphs from:

- **Inter Regular** and **Inter Bold** — Rasmus Andersson and the Inter
  project, SIL Open Font License 1.1 (`OFL-Inter.txt`).
- **JetBrains Mono Regular** — JetBrains, SIL Open Font License 1.1
  (`OFL-JetBrainsMono.txt`).

These are the same families and pixel sizes KR32 and KR16 use: Inter 14 px for
labels, JetBrains Mono 15 px for source and disassembly, and JetBrains Mono
13 px for secondary text.

## Provenance

The three `*-Subset.ttf` files were copied unchanged from the sibling KR16
checkout's `resources/fonts/` directory on 2026-11-20. They are Latin-1 subsets
(`U+0020`–`U+00FF`, no layout features, no hinting) produced by `subset.sh`.
The source families are Inter and JetBrains Mono under the OFL; the full
upstream files are not committed.

## Regeneration

1. Put `Inter-Regular.ttf`, `Inter-Bold.ttf` and
   `JetBrainsMono-Regular.ttf` in this directory.
2. `bash subset.sh` (requires `pyftsubset` from fonttools).
3. `python third_party/fonts/embed_fonts.py`.

`src/ui/embedded_fonts.cpp` and `src/ui/embedded_fonts.h` are generated and
committed, so normal builds need neither Python nor fonttools.
