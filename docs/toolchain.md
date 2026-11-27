# Host toolchain

The host runs the installed KRCompiler package; it never links the compiler and
never reads a KRCompiler build tree.

## Targets

krdbg loads four image formats. Source builds through `krc`/`krld` cover three of
them; a kr216 image is loaded directly.

| Target | Image | Source build in krdbg |
|---|---|---|
| KR32 | `.kr32` | yes |
| KR16 | `.kr16` | yes |
| KR216 | `.kr216` | no — load the image with `--kr216` or `--load` |
| KR232 | `.kr232` | yes |

The matching adapter must be built in (`KRDBG_KR32_ADAPTER`,
`KRDBG_KR16_ADAPTER`, `KRDBG_KR216_ADAPTER`, `KRDBG_KR232_ADAPTER`). The default
presets build with all four off.

The image fixtures under `tests/fixtures/<vm>/` are checked in. Rebuild them
with the `regenerate.ps1` or `regenerate.py` in that directory when their source
changes; each directory's README names the inputs.

## Pin

KRCompiler `402cfaf` (item 4.14) installs:

```text
<prefix>/bin/krc
<prefix>/bin/krld
<prefix>/share/krc/sysroot/kr32/lib/{crt0.s,fileio.s,eh.s,libkrcrt.kro}
<prefix>/share/krc/sysroot/kr16/lib/{crt0.s,libkrcrt.kro,...}
<prefix>/share/krc/sysroot/kr216/...
<prefix>/share/krc/sysroot/kr232/lib/{crt0.s,...}
```

Pass the installed prefix with `--toolchain <prefix>` or set
`KRDBG_KRC_PREFIX`. `krdbg_host` validates `bin/krc`, `bin/krld` and the sysroot
before running either tool.

Configuring krdbg itself needs the package: pass `-DCMAKE_PREFIX_PATH=<prefix>`
to the configure step. `scripts/gate-windows.ps1` resolves that prefix from
`KRG_PREFIX`, `KRG_KRC` or `../build/family-prefix` and passes it; on Linux the
package must already be discoverable by CMake. The vendored KDBG copy under
`third_party/krc-debug/` supplies decoder source and the parity test; it does not
remove the `krc-debug`/`krc-toolchain` package requirement.

## Host commands

```sh
# build and load a C, C++ or assembly source
krdbg_demo --headless --load tests/fixtures/kr16/m4.c --target kr16 \
    --toolchain build/m5-toolchain
krdbg_demo --headless --load source.cpp --target kr32 \
    --toolchain build/m5-toolchain
krdbg_demo --headless --load source.c --target kr232 \
    --toolchain build/m5-toolchain

# load an existing image directly
krdbg_demo --headless --load tests/fixtures/kr16/m4.kr16 --source-root .
krdbg_demo --headless --load image.kr216
krdbg_demo --headless --load image.kr232

# rebuild a source-origin program (RESET)
krdbg_demo --headless --load tests/fixtures/kr16/m4.c --target kr16 \
    --toolchain build/m5-toolchain --reset

# load the embedded KR32OS pair
krdbg_demo --headless --kernel
```

`--source-root` names the directory used to resolve KDBG source paths; the
default is the process working directory. Paths are normalized to `/` before
resolution.

## Build recipes

- KR32 C/C++: `krc --target=kr32 --sysroot=<prefix>/share/krc/sysroot -O0 -c
  <source> -o <object>`, then `krld <object> <sysroot>/kr32/lib/crt0.s
  <sysroot>/kr32/lib/fileio.s <sysroot>/kr32/lib/eh.s
  <sysroot>/kr32/lib/libkrcrt.kro -o <image> --base=0x10000 --entry=_start
  --source-root=<root>`.
- KR32 assembly: the same `krld` link with the `.s` input instead of an object.
- KR16 C/C++: `krc --target=kr16 --sysroot=<sysroot> -O0 -c <source> -o
  <image>.kr16`.
- KR16 assembly: `krc --target=kr16 --emit-object -c <source> -o <object>`;
  assemble the installed `crt0.s` the same way and link both KRO objects with
  `krld --base=0x100 --entry=_start`.
- KR232 C/C++: compile the source and the installed `kr232/lib/crt0.s` to KRO
  objects, then link every `.kro` in `kr232/lib` with `krld -o <image>.kr232
  --entry=_start`. A kr232 container records its own base, so the link takes no
  `--base`.
- KR216: `krc --target=kr216 ...` produces a `.kr216` image, but krdbg's host
  build chain does not drive it. Load the image directly.

The external settings contract is `krdbg_settings --describe-settings` and
`krdbg_settings --settings <file.json>`.
