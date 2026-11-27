# KR232 fixtures

Five checked-in containers exercise the kr232 adapter. They are built by
`regenerate.ps1` from KRCompiler and kr232 sources, not hand-made:

| Fixture | Used for |
|---|---|
| `loaded.kr232` | layout, memory regions and the loaded-state golden |
| `nested_frames.kr232` | two-frame unwind and a frame-relative local |
| `session.kr232` | COM output and a committed framebuffer |
| `kernel.kr232` | the kernel the adapter runs a user program under |
| `userprint.kr232` | a user program that prints through the kernel and exits 3 |

`host_build.c` is a source file compiled at test time by
`tests/test_host_build.cpp`; it is not a fixture container.

## Regenerate

From the repository root, with `krc` and `krld` built and `KR232_SOURCE`
pointing at the kr232 checkout:

```powershell
$env:KRC = "path/to/krc.exe"
$env:KRLD = "path/to/krld.exe"
$env:KR232_SOURCE = "path/to/kr232"
./tests/fixtures/kr232/regenerate.ps1
```

The script writes the five containers here with forward-slash source paths so a
Windows build and a Linux build record the same paths. `CheckFixtureDrift.cmake`
rebuilds them when `KRDBG_KR232_KRC` and `KRDBG_KR232_KRLD` are set and compares
the result byte for byte.
