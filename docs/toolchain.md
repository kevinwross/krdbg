# Host toolchain

The host runs the installed KRCompiler package; it never links the compiler and
never reads a KRCompiler build tree.

## Pin

KRCompiler `402cfaf` (item 4.14) installs:

```text
<prefix>/bin/krc
<prefix>/bin/krld
<prefix>/share/krc/sysroot/kr32/lib/{crt0.s,fileio.s,eh.s,libkrcrt.kro}
<prefix>/share/krc/sysroot/kr16/lib/{crt0.s,libkrcrt.kro,...}
```

Pass the installed prefix with `--toolchain <prefix>` or set
`KRDBG_KRC_PREFIX`. `krdbg_host` validates `bin/krc`, `bin/krld` and the sysroot
before running either tool.

## Host commands

```sh
# build and load a C, C++ or assembly source
krdbg_demo --headless --load tests/fixtures/kr16/m4.c --target kr16 \
    --toolchain build/m5-toolchain
krdbg_demo --headless --load source.cpp --target kr32 \
    --toolchain build/m5-toolchain
krdbg_demo --headless --load source.s --target kr16 \
    --toolchain build/m5-toolchain

# load an existing image directly
krdbg_demo --headless --load tests/fixtures/kr16/m4.kr16 --source-root .

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

The external settings contract is `krdbg_settings --describe-settings` and
`krdbg_settings --settings <file.json>`.
