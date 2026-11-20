# Third-party assets and licenses

Every third-party asset in this repository, its license, and where it came
from. The font and `stb_truetype` copies are committed; nothing is downloaded
at build time.

| Asset | Version | License | Location | Provenance |
|---|---|---|---|---|
| stb_truetype | 1.26 | Public domain / MIT | `third_party/stb/stb_truetype.h` | Copied unchanged from the sibling KR32 checkout on 2026-11-20; license text is at the end of the header and in `third_party/stb/README.md`. |
| Inter Regular | 3.19 subset | SIL Open Font License 1.1 | `third_party/fonts/Inter-Regular-Subset.ttf`, `OFL-Inter.txt` | Copied unchanged from the sibling KR16 `resources/fonts/` directory on 2026-11-20; Latin-1 subset, no hinting or layout features. |
| Inter Bold | 3.19 subset | SIL Open Font License 1.1 | `third_party/fonts/Inter-Bold-Subset.ttf`, `OFL-Inter.txt` | Same as Inter Regular. |
| JetBrains Mono Regular | 2.304 subset | SIL Open Font License 1.1 | `third_party/fonts/JetBrainsMono-Regular-Subset.ttf`, `OFL-JetBrainsMono.txt` | Same as Inter Regular. |
| SDL2 | 2.32.x (system) | zlib | Not vendored | Linux-only runtime dependency discovered with `find_package(SDL2 CONFIG)` or `pkg-config`; never linked on Windows. |

Regeneration instructions are in `third_party/fonts/README.md` and
`third_party/stb/README.md`.

No other third-party code, font, or binary asset is present. `third_party/` is
not a build output; the generated `src/ui/embedded_fonts.cpp` is committed so a
build needs no Python or fonttools.
