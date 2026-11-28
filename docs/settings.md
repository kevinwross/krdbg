# Settings

krdbg renders one schema-driven settings dialog over every component: the VM
(D layer), the compiler, linker and assembler providers, the debugger's source
settings, and the debugger's own presentation preferences. This page describes
the files, format, layers, per-target defaults and current limits. `PLAN.md`
§4.10 is the design of record.

## Files

| File | Owner | Purpose |
|---|---|---|
| `%APPDATA%\krdbg\settings.json` (Windows) | krdbg | The user document. The only settings file krdbg writes. |
| `$XDG_CONFIG_HOME/krdbg/settings.json` or `~/.config/krdbg/settings.json` (Linux/macOS) | krdbg | The same user document. |
| `<project>/.krdbg/settings.json` | the checkout | A project document: overrides that apply to this checkout. Read-only to the debugger. |

The user file is created lazily on the first successful `APPLY` or `--set`; its
parent directories are created with it. Starting krdbg never creates a file. A
missing file means "no user overrides", not an error; a malformed one is
reported and startup continues without the user layer.

The project file is discovered from the path being debugged: the first
`.krdbg/settings.json` found by walking up from the loaded source or image (or
the configured workspace/source root) to the filesystem root. The loaded path's
directory is the start when the path is a directory. Saving never touches the
project file; the dialog's `SAVE: PROJ` layer applies the change to the session
and reports that the project file is read-only.

## Format

One JSON document with a `shared` section and one section per stable target id
(`kr32`, `kr16`, `kr216`, `kr232`; a source load uses its build target):

```json
{
  "version": 2,
  "shared": {
    "values": {"cc.target": "kr232"},
    "origins": {"cc.target": "user"}
  },
  "targets": {
    "kr232": {
      "values": {"cc.optimization": "-O2", "dbg.source_roots": ["../krcompiler"]},
      "origins": {"cc.optimization": "user", "dbg.source_roots": "user"}
    }
  }
}
```

- A descriptor carries a `scope`: `shared` for app-level settings (the target
  selectors, presentation preferences), `target` for everything that depends on
  the VM. An unset or unknown scope is `target`, so a new VM setting cannot
  become global by accident.
- Values are written to the section their descriptor's scope names. The user
  document's sections are sparse: a value still at its default is not written,
  so a later default change reaches an existing profile.
- `origins` are informational and written for diagnostics; the layer a document
  is loaded as (user or project) decides the dialog badge.
- A version-1 flat file (`{"version": 1, "values": ...}`) loads as the `shared`
  section and is rewritten in document form on the next save.
- A value id the current schema does not own is ignored on load. An unknown
  target section is retained as loaded and written back, so a newer host's
  targets are not destroyed by an older save.
- Files are UTF-8 JSON. Section and value order in the file is not significant.

## Layers and precedence

Lowest precedence first:

1. **Code defaults** — each descriptor's canonical `default_json`, overlaid by
   the active target's defaults from the per-target table.
2. **User shared** — the user document's `shared` section.
3. **User target** — the active target's section of the user document.
4. **Project shared** — the project document's `shared` section.
5. **Project target** — the active target's section of the project document.
6. **Command line and environment** — a flag or variable where one exists
   (`--target`, `--source-root`, `KRDBG_KRC_PREFIX`, ...).

A value missing from every layer is synthesised from its canonical default, so
the dialog shows the real value with a `D` badge instead of an empty
placeholder. Switching targets re-resolves the layers: the new target's sections
replace the previous target's and its defaults replace the global ones.

`--set ID=VALUE` writes a `target` setting to the active target's section (or to
`shared` when no target is known yet) and a `shared` setting to `shared`. It
saves the user document immediately. `--target NAME` chooses the active target
before the first load, so `--target kr232 --set dbg.source_roots=...` writes the
kr232 section; a source loaded with `--load` switches to the target it was built
for and re-applies that section.

## The target-defaults table

A default whose correct value depends on the VM comes from a table in
`src/core/tool_settings.cpp`, keyed by the stable target id. The table is the
host-side placeholder for the design's D layer: a VM owns its own values
eventually. Each row names its class and carries a provenance string.

| Class | Meaning |
|---|---|
| `Capability` | A guest-visible fact the target fixes. Its descriptor is read-only: shown in the dialog, never written to a settings document and never overridable. |
| `Device` | An emulated-device setting the VM API lets the host change. Editable and stored per target when such a setting exists; no VM exposes one today. |
| `Setting` | A default the host owns for the target (compiler level, linker base, instruction slice). |

Only values that are known are listed. A developer's source-tree path or a
machine-specific memory size belongs in user or project settings, never in the
table. A target with no row for a setting uses the descriptor's default, and a
capability value of `0`, `false` or empty means "this target does not report
it" unless the capability's description names it.

Current rows:

- `kr32`: `-O1`; `ld.base`/`as.base` `0x10000`; `vm.time_slice` 100000;
  capabilities: 32-bit pointers, RAM 128 MiB, keyboard FIFO 64, serial-tty input
  with a 4096-byte RX buffer, framebuffer aperture 32 MiB, default reset mode
  1920x1080, terminal scrollback 256 lines, video device.
- `kr16`: `-O1`; `ld.base`/`as.base` `0x100`; `vm.time_slice` 200000 (see
  below); capabilities: 16-bit pointers, 64 KiB address space with 60 KiB usable
  RAM, keyboard FIFO 64, output-only console (no input queue), no framebuffer or
  video device.
- `kr216`: `-O0` at bring-up (owner to confirm; `-O1` is measured, not
  assumed); `ld.base` `0x200`; capabilities: 16-bit pointers, 64 KiB address
  space, console at `0xFF00`/`0xFF02`/`0xFF04` with input, no framebuffer or
  video device.
- `kr232`: `-O2`; `vm.time_slice` 100000 (krdbg's own choice — kr232 has no
  equivalent project setting); capabilities: console input queue 16, keyboard
  FIFO 64, framebuffer aperture 32 MiB, default Frame 800x600 with a 3200-byte
  stride in XRGB8888, maximum mode 1920x1080, terminal 80x30, committed-frame
  video only, one cycle per retired instruction, all devices mandatory, RAM
  256 MiB.

### Optimization

`cc.optimization` accepts `-O0`, `-O1` and `-O2`, and the demo host passes the
resolved level into the source build (`BuildOptions::optimization` → `krc`), so a
target default is not inert. `src/host/build_chain.cpp` rejects any other level
before starting the compiler.

- `kr232` defaults to `-O2`. Recorded allocas stay in their frame slots at `-O2`
  (slot protection), so a named local remains inspectable. `-O2` may inline small
  internal functions, which removes their frame and coarsens the line table, and
  it may change call stacks.
- `kr32` and `kr16` default to `-O1`: those backends promote locals at `-O2`, so
  the frame-slot guarantee holds only at `-O1`.
- `kr216` stays at `-O0` during bring-up; the kr216 owner must confirm any move.

## Presentation preferences

These are `shared` UI settings. Their defaults record today's built-in
behaviour: COM scrollback 5000 lines, COM input cap 4096 bytes (candidate, low
confidence, owner to confirm), video pop-out window 640x480, and video fit
`fit`. The surface still uses the built-in constants; a follow-up that wires the
keys changes no default.

## Current limits and owner-to-confirm items

- `vm.time_slice` is an instruction budget per screen update, not a wall-clock
  interval. krdbg has no wall-clock guard, so kr16's 200000 (its GUI's 8 ms
  tick) is an approximation, not a time conversion.
- `ld.base` and `as.base` are inert for kr232: its container records its own
  base, so the build chain does not pass `--base` for it. Both are marked
  advanced so they are hidden by default.
- No VM API exposes a configurable device setting in this host, so the table has
  no `Device` rows. Capability rows are the guest-visible contract, not controls.
- krdbg's kr16 adapter is BIOS-less while the kr16 VM's command-line defaults
  BIOS on. krdbg keeps its current behaviour (off); the kr16 owner should
  confirm which is intended.
- The surface reads the instruction slice as the qualified `vm.time_slice`; it
  previously looked up an unqualified `time_slice`, so the setting never applied.
- A capability row with a `0`/`false`/empty value on a target that has no
  evidence means "not reported", not "absent". The `Capability` rows above list
  the evidence that exists today.
