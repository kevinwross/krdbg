# kr232 debug-symbol gaps

**Status 2026-11-27T20:45 — resolved through phase 2.** The BLOCKS and
FIX-BEFORE findings G1–G8 are fixed in KRCompiler `923bca2` and `fcbf8f7`
(truthful per-file lines including prologues, F for every function with display
names and per-object labels, internal-linkage G, dedupe, V/W/K and home-slot
protection); the ERRATA G9–G12 (line-table size, column 0, unused function
source/line) remain quality notes. kr232 now builds at `-O2` by default: named
locals keep their home slots because recorded allocas are never promoted, while
small internal helpers may be inlined and their frames/locals disappear (choose
`-O1` for faithful stepping). No backend emits a `P`/Location record yet. The
statements below describe the tree at audit time and are kept as the record.

Audit of debug-symbol emission for the kr232 backend against the KR32 reference,
against the current working tree of `C:\Users\kevin\repos\krcompiler` (HEAD
`a9e22a0` plus the uncommitted `.debug_line` WIP). Written 2026-11-27.

**Verdict.** The WIP fixes the bulk of the historical line-table defect — a
rebuilt `startrekng` image is 55/32,307 out-of-range line records (0.17%) where
the image in `build/startrekng-kr232` (built with the installed prefix compiler)
is 42,719/44,785 (95%). It is not complete or correct yet: the function
definition line still gets no record at the function entry, every function's
prologue inherits the previous function's last line, and the F/G/V/W/K half of
the contract is still materially below KR32. Two gaps block source-level
debugging outright (G1, G2); six are FIX-BEFORE (G3-G8); four are ERRATA
(G9-G12).

The 44 internal functions that have no F record in the rebuilt image, and the
absence of a record at the definition line, are the two findings that change what
a user sees on almost every stop.

## How the evidence was gathered

Toolchain under test: the `windows-msvc-release` build of `krc.exe`/`krld.exe`
(`C:\Users\kevin\repos\krcompiler\build\windows-msvc-release\host\bin\Release\`),
rebuilt 2026-11-27 11:17:01 after the working-tree edits at 11:16:43/11:16:56, so
it contains the WIP. (`--version` still prints revision `80960f0` because
`kBuildRevision` is baked at configure time; the emitted `.debug_line`
directives and KDBG type records prove the newer tree is compiled in.)

The WIP itself, for the record:

```text
$ git -C C:\Users\kevin\repos\krcompiler status --short
 M src/asm/kr232_assembler.cpp
 M src/backend/kr232_backend.cpp
$ git -C C:\Users\kevin\repos\krcompiler diff --stat
 src/asm/kr232_assembler.cpp   | 16 ++++++++++++----
 src/backend/kr232_backend.cpp |  9 +++++++++
 2 files changed, 21 insertions(+), 4 deletions(-)
```

Image records were read with `krc --dump-symbols IMAGE` (text). **The text dump
prints `line` records; the JSON dump does not** (`src/debug/kdbg.cpp:805-808`
versus `dump_kdbg_json`, which has no lines array). krdbg has no
`KRDBG_PROBE_IMAGE` hook (`grep -r KRDBG_PROBE_IMAGE krdbg` finds nothing), so
krdbg's own translation was not executed; krdbg impact below is derived from its
code paths. No krdbg binary exists in the tree to run.

**krdbg snapshot.** At the start of the audit krdbg was clean at HEAD `71e4591`;
the krdbg file:line citations below are that snapshot. By the end of the audit
the working tree carried an uncommitted consumer change in seven files
(`include/krdbg/debug_info.h`, `src/core/debug_info.cpp`,
`src/core/debug_info_util.h`, `src/core/session.cpp`,
`src/core/variables.cpp`, `src/core/variables.h`, `src/kdbg/kdbg.cpp`) that
already adds `LocationInfo`/`location_at()`/P translation,
`FunctionInfo::symbol` + `identity()`, `VariableSlotUnusable` and an
`<optimized out>` value. G3 and G7 below are written to build on that change;
their krdbg line numbers refer to HEAD and will shift when it lands.

Scratch work was done under `krdbg/build/kr232audit/` and `%TEMP%\kr232audit\`
and removed afterwards; every command below reproduces from the sources in it.
Test inputs: `t1.c` (`g_exported`, static `g_internal`, `struct Point g_point`,
`static int helper`, `main` with a scalar, a struct local and a static local),
`t2.c` (one global function), `t3.cpp` (`struct Widget` member function, static
`internal_helper`, `main_cpp`), `t4.s` (hand-written assembly), `t5a/t5b.cpp`
(two TUs both defining an inline function), `t6.c` (arrays, union, enum, pointer
globals and locals), `t7.s`/`t8.s` (hand-written `.debug_global`/`.debug_line`/
`.debug_var`), `t9.cpp` (nested scopes, temporaries, static helpers).

Key commands (paths elided; `$KRC`/`$KRLD` are the release binaries):

```text
$KRC --dump-symbols IMAGE
$KRC -S  --target=kr232 -O0 --sysroot=<prefix>/share/krc/sysroot t.c -o t.s
$KRC -c  --target=kr232 -O0 --sysroot=... t.c -o t.kro
$KRLD --entry=main t.kro -o t.kr232
$KRC -c  --target=kr232 t1-O0.s -o t1-from-s.kro       # reassemble generated .s
# Reference builds: same commands with --target=kr32.
# Rebuilt real image: the 12 krc -c invocations of scripts/build-startrekng-kr232.ps1
# (8 example units, crt0.s, libc_full.c, fileio_full.s, main_script.cpp), then
# $KRLD --entry=_start crt0.o <8 units> main_script.o libc.o fileio.o -o startrekng_script.kr232
```

The shipped image `build/startrekng-kr232/startrekng_script.kr232` is stale for
this audit: `build-info.txt` records that it was linked at 11:22 with the
*installed prefix* compiler (`krc 0.2.0, revision 80960f0`, four krcompiler
commits behind HEAD), and it has `types=0 symbols=0 varinfo=0 scopes=0`. Numbers
labelled "rebuilt image" below come from the same sources built with the current
release compiler.

## Ranked gaps

| ID | Severity | Gap | Blocked krdbg behaviour |
|---|---|---|---|
| G1 | **BLOCKS** | No L record at the function definition line; prologue/entry line wrong (WIP incomplete vs ADR 0061) | breakpoint on `{`, line stepping from entry, frame-creation visibility |
| G2 | **BLOCKS** | Internal/static/synthetic functions have no F record (44 in the rebuilt image); their lines are dropped by the linker | call-stack name, locals in those frames, line stepping/breakpoints inside them, step-over/into |
| G3 | FIX-BEFORE | F records carry no `debug_name`; call stack shows `__krc_user_main`, `Widget_get`, `Computer_dtor` | call-stack naming (the K-scope identity path in G3 is the prerequisite) |
| G4 | FIX-BEFORE | Duplicate and stale F/V records for weak functions across TUs (8 dtor names ×5; 34 duplicate V records; a stale span `0x5fe0-0x98dc`) | duplicate `this` in Variables, wrong function attribution in dead ranges |
| G5 | FIX-BEFORE | G records only for `.globl`/`.weak` data: every static/internal global is absent (all of `libc_full.c`'s statics, `navigationDx`, `kRegionSuffixes`, `t1.c`'s `g_internal`) | Globals window omits all file-scope statics |
| G6 | FIX-BEFORE | V records include compiler temporaries; no W (kind/scope/flags) and no K scopes; parameters and `this` are indistinguishable from locals | Variables window clutter, params/`this` unlabelled, shadowed locals ambiguous |
| G7 | FIX-BEFORE | Promoted locals: at default `-O1` a shadowed named local is promoted despite the recorded-variable guard; at `-O2` all named scalars/params lose their V record; no P records exist anywhere and krdbg ignores Location records | a local disappears from Variables while live (maintainer requirement) |
| G8 | FIX-BEFORE | `-S` then `-c` mixes the generated `.s` path with C line numbers (`.debug_line` carries no file) | breakpoints/stepping in the split compile workflow |
| G9 | ERRATA | F frame size and return type are placeholders (`frame=0 type=0`), no function end label | none in krdbg today (kr232 adapter unwinds on ENTER; kr32 adapter needs frame) |
| G10 | ERRATA | One L record per instruction: ~2× KR32's line table (t1 69 vs 38; t6 207 vs 101; t9 155 vs 89) | image size only |
| G11 | ERRATA | L column always 0 (KR32 writes 1/5/…) | none (krdbg's `LineInfo` has no column) |
| G12 | ERRATA | No `FunctionInfo.source`/`line` in KDBG / krdbg translator (kdbg.cpp:116-117) | no consumer today |

Shared with KR32 (not kr232-specific, listed so they are not re-reported): union
types are recorded as `C` and decode as `kind=struct` with flattened member
offsets; enums decode as their integer type; `_ZTV*`/`_ZTI*`/`_ZTS*` data objects
appear in Globals. `t6.c` dumps for kr32 and kr232 are identical in type shape,
size and member offsets.

---

## G1 — BLOCKS — No definition-line record; entry line wrong

**What the WIP does.** `Kr232Backend::emit` writes `.debug_line <definition.line>`
at `krcompiler/src/backend/kr232_backend.cpp:1975-1977`, which is emitted after
the prologue (`enter`, line 1763), the parameter spills (1797-1822) and the
`.debug_var` records (1767-1788) — immediately before the block loop at 1979.
The per-instruction changes follow at 1979-1986.

**Why that misses the contract.** `krcompiler/docs/symbols.md:73-79` and
ADR `docs/decisions/0061-function-definition-location.md` require the definition
line record at the address of `.L<symbol>.begin`: "Every function's entry address
carries a line record for the source line of its definition", and a breakpoint on
the definition line must bind "before the prologue". The backend emits
`.L<name>.begin` at `kr232_backend.cpp:1596`, but the first `.debug_line` for the
function is emitted many instructions later, and is immediately superseded when
the first instruction has a different line. There is also no `.L<name>.end`
label (the F record's end comes from the next text label; see G9).

**Evidence.**

* `t2.c` (5 lines, one function). Generated assembly:
  line 7 `.Lscript_entry.begin:`, line 8 `enter #20, #0`, line 9-10
  `.debug_var`, line 11 `st [r13-4], r2`, line 12 `.debug_line 2`, line 13
  `.Lf0_bb0:`. The image's first records are `line 0x34c source=1 line=7`,
  `line 0x350 source=1 line=10`, then `line 0x353 source=1 line=2`. The record
  at the function entry (0x34c) names **generated-assembly line 7**, not line 2;
  line 2 binds at 0x353, after the frame exists.
* `t1.c` at `-O0`: `__krc_user_main` (`main`, definition on source line 10) has
  **no record for line 10 at all**. Records by line are
  `8:1, 11:2, 12:13, 14:12, 15:41`; the single line-8 record sits at the
  function entry 0x254 and is `helper`'s `return sum` line (helper is static and
  has no F record, see G2, so its own line records are dropped by
  `linker.cpp:973-985`). A breakpoint on line 10 can never bind.
* `t3.cpp`: `main_cpp`'s entry record is line 6, the last line of the static
  `internal_helper` emitted before it; then correct lines 8-11 follow.
* Rebuilt `startrekng` image: 248 of 328 F ranges begin with an L record whose
  line equals the previous function's last line (prologue inherits the assembler
  state left by the prior function). 55/32,307 records are out-of-range for
  their file, e.g. `examples/startrekng/game.cpp` is 313 lines but records reach
  line 2311, `main_script.cpp` is 8 lines with 45 out-of-range records to 220
  (the first function of each TU is still assembled with generated line numbers
  because no `.debug_line` has been seen yet).
* The `.s` source path is genuinely correct for hand-written assembly: `t4.s`
  dumps `line 0x200 line=5 … line 0x20b line=9`, matching `.s` lines 5-9. That
  path must not regress.

**Impact on krdbg.**

* *Breakpoints*: `DebugSession::Impl::resolve` searches for the first line
  record matching the requested file+line (`krdbg/src/core/session.cpp:125-141`,
  lines sorted by address at `src/kdbg/kdbg.cpp:135-138`). A definition-line
  breakpoint either never resolves (t1 line 10) or binds mid-prologue after
  `enter` (t2 line 2). This is exactly the failure ADR 0061 was written for.
* *Source stepping*: `step(LineIn/LineOver)` keys on `line_at(pc)`
  (`session.cpp:458-518`); the prologue reports a line from the previous
  function, so a step from the entry can jump backwards into unrelated source or
  fail to leave the line. Out-of-range lines have no matching row in the source
  panel (`src/ui/surface.cpp:1670-1684` scrolls to `line-1`, and
  1722-1724 highlights only an existing row), so the PC stops with **no
  highlight at all**.
* *Frame creation*: stepping from the definition line cannot show `enter`
  creating the frame, which ADR 0061 lists as a consequence of the fix.

**Proposed change.**

1. `krcompiler/src/backend/kr232_backend.cpp`, `Kr232Backend::emit`, in the
   function loop: immediately after line 1596 (`out << ".L" << name <<
   ".begin:\n";`) and before the prologue, emit
   `.debug_line <function->definition.line>` and initialise the
   `current_debug_line` that is now declared at line 1978. Delete the late
   emission at 1975-1978. Keep the per-instruction change test at 1982-1986.
   The L record then lands at the function symbol address, satisfying
   symbols.md:73-76.
2. Synthetic functions (`__krc_init_globals`, the generated `main` wrapper,
   `__krc_member_dtor_*`) have no valid definition. For them the backend must
   not leave the assembler's line state pointing at the previous function.
   Extend the assembler directive with an explicit "no source line" form:
   `krcompiler/src/asm/kr232_assembler.cpp:111-113` (state) and 153-157
   (parsing) hold a `std::uint32_t debug_line` whose 0 currently means "fall
   back to the assembly line"; make it a tri-state (no directive seen → the
   assembly line is the source, as for hand-written `.s`; `.debug_line 0` →
   emit no L record until the next directive; `.debug_line N` → line N). The
   backend then emits `.debug_line 0` at the entry of a function whose
   definition and first instruction have no location, or the first instruction's
   line when only the definition is missing.
3. For the location itself, `debug_line` records must stay byte-compatible with
   the addresses of `.L<name>.begin` and `.L<name>.end`; the assembler's
   `DebugLine` push sites are 276-278, 291-292, 362-363, 411-412, and the bridge
   labels come from 542-547.

Acceptance for this gap: for every function with a valid definition,
`krc --dump-symbols IMAGE` has an L record whose address equals the F record's
`start` and whose line equals the definition line; no L record for a source file
has a line greater than the file's line count.

## G2 — BLOCKS — F records only for external/weak functions

**What the code does.** The kr232 F records are not emitted from the IR; the
assembler synthesises them from text labels, and only for labels marked
`.globl`/`.weak` (`krcompiler/src/asm/kr232_assembler.cpp:561-570`; the filter is
`if (globals.count(name) == 0) continue;` at 563). The backend marks a function
exported only for non-internal linkage (`kr232_backend.cpp:1586-1590`). So every
`static` C/C++ function, every internal C++ destructor helper and every
synthesized internal function is invisible.

**Evidence.**

* `t1.c -O0`: `helper` (static) has no F record; image has
  `function __krc_user_main 0x254-0x305` and `function main 0x333-0x349` only.
  KR32 from the same source: `function helper 0x10000-0x10078 frame=56 type=i32`,
  plus `__krc_init_globals` and the `main` wrapper.
* `t3.cpp`: static `internal_helper` absent; `Widget_get` and `main_cpp` present.
* `t9.cpp`: `make` and `add` (both static) absent; their V records exist
  (`variable make.a`, `make.v`, `add.result`, …), so they are recorded but
  unreachable.
* Rebuilt `startrekng` image: 328 F records. Of the generated text labels, 12 in
  the eight example TUs and 32 in `libc_full.c` are emitted **without**
  `.globl`/`.weak` — 44 functions missing: `insideGalaxy` (scanner.cpp and
  weapons.cpp), eight `__krc_member_dtor_*` in game.cpp,
  `__krc_init_globals` (galaxy.cpp, weapons.cpp) and 32 libc helpers
  (`__krc_pair64_*`, `emit_char`, `print_unsigned`, `print_hex`, …).
* The build's linker then erases every L record that lies outside an F range
  (`krcompiler/src/linker/linker.cpp:973-985`), so all source-line records for
  those 44 functions are thrown away. This is also why `helper`'s line records
  (lines 6-8) do not exist in `t1`'s image — the one line-8 record that does
  appear sits at `__krc_user_main`'s entry because the assembler's line state
  leaked across the function boundary (G1).

**Impact on krdbg.**

* *Call stack*: `call_stack()` names each frame with
  `function_name(find_function(pc))` (`session.cpp:626-643`); with no F range
  the name is empty and the panel renders `(unknown)`
  (`src/ui/surface.cpp:2989-2993`). Any stop inside a static helper, a
  destructor helper or `__krc_init_globals` shows an anonymous frame.
* *Locals/params*: `locals(frame)` returns immediately when `find_function`
  returns null (`session.cpp:654-663`), so a frame in a static function shows
  `(no locals)` even though its V records exist.
* *Source stepping and breakpoints*: `find_line` clamps its search to the
  enclosing function (`src/core/debug_info_util.cpp:175-228`); with no enclosing
  function it falls back to "the last line at or before pc", which for code
  between functions is the previous function's line — stepping through a static
  function shows the caller's stale line. Line records inside it were already
  dropped, so a breakpoint on a line of a static function cannot resolve.
* *Step-over*: `StepMode::InstructionOver`/`LineOver` detect entering a call by
  a function change plus unwind depth (`session.cpp:406-436`, 484-518); a null
  start/current function disables the function-change signal, so step-over can
  run to the step guard or stop inside the callee.

**Proposed change.** Stop deriving F from symbol linkage; make the backend state
the function explicitly. This one directive also fixes G3/G9's F fields.

1. `krcompiler/src/backend/kr232_backend.cpp`, `Kr232Backend::emit`, function
   loop at 1574-1597: after the signature is final (`frame` at 1761, return type
   at 1627-1632) emit, immediately before the symbol label:
   `.debug_function <display> <symbol> <begin_label> <end_label> <frame>
   <ret_type>`
   with `<begin_label>` = `.L<name>.begin`, `<end_label>` = `.L<name>.end`, and
   `<ret_type>` from `debug_type_name(function->type->return_type)` (or `void`).
   Emit `.L<name>.end:` after the return sequence (`kr232_backend.cpp:2594-2633`,
   after the `leave #0`/`ret` pair) so the F end is known without relying on the
   next function's label. Do it for every non-declaration function, internal and
   synthetic included.
2. `krcompiler/src/asm/kr232_assembler.cpp`: parse the directive next to the
   existing debug directives (146-171); replace the heuristic F loop (548-570)
   with records built from the directives. Because two translation units can
   define internal functions with the same source name (`insideGalaxy` is already
   duplicated that way), the F record's start/end must be per-object labels the
   assembler synthesises, not names: add symbols
   `label_prefix + "fn" + ordinal + "_begin"` / `"_end"` at the function's offset
   and at its end offset (the existing hash prefix is built at 527-539 and
   already keeps line labels unique), and emit `F <display> <begin> <end>
   <frame> <type>` with those labels. Keep the current `.globl`-label heuristic
   only as a fallback when no `.debug_function` record exists (hand-written `.s`).
3. `krcompiler/src/asm/kr232_assembler.cpp:510-518`: keep the bridge record order
   (S, types, L, F, G, V) so `attach_kdbg` continues to parse it.

Acceptance: every text-label function has an F record; the F record's `start`
equals the address of its `.L.begin` label; no line record of any file is
dropped by `linker.cpp:973-985` for a reason other than a peephole-removed
instruction (the case that filter exists for).

## G3 — FIX-BEFORE — No display name (`debug_name`); krdbg cannot resolve it

**What the reference does.** KR32 writes
`F <display_name> <symbol> .L<symbol>.ret <frame> <type>`
(`krcompiler/src/backend/kr32_backend.cpp:1498-1510`), with
`display_name = function.debug_name.empty() ? function.name :
function.debug_name`, and IRGen sets `debug_name` to `Class::method`,
`Class::Class`, `Class::~Class`, or the source name (ADR 0030,
`krcompiler/docs/decisions/0030-debug-display-names.md`). The kr232 backend never
reads `debug_name` (no occurrence in `kr232_backend.cpp`) and the assembler
writes `F <name> <name> …`, so the symbol is the display name.

**Evidence.**

| | kr232 (`t1`, `t3` dumps) | KR32 (same sources) |
|---|---|---|
| user main | `function __krc_user_main 0x254-0x305 frame=0 type=0` | `function main 0x10084-0x10198 frame=88 type=i32` (symbol `__krc_user_main`) |
| C++ method | `function Widget_get 0x200-0x254 frame=0 type=0` | `function Widget::get 0x10000-0x10094 frame=72 type=i32` |
| C++ dtor (real image) | `function Computer_dtor 0x5fe0-…` | display form is `Computer::~Computer` (IRGen: `c_irgen_setup.cpp:361` sets `debug_name = tag + "::~" + tag`) |

**Impact on krdbg.** The call stack currently shows the implementation name for
the user's `main` and for every C++ method/destructor. But adopting the KR32
spelling naively regresses locals, because krdbg resolves variables by the F
*name*:

* `FunctionInfo.name` is the KDBG F name (display name) — `src/kdbg/kdbg.cpp:110-124`.
* `VariableInfo.function` is the V record's function, which is the symbol name
  (`kdbg.cpp:156-187`).
* `locals()` keeps a variable only when `variable.function == function->name`
  (`session.cpp:654-663`).

So with KR32-style display names, `main`'s frame would ask for variables of
`__krc_user_main`, find none, and show `(no locals)`; the same for
`Widget::get` vs `Widget_get`. This is an existing latent krdbg bug that the
KR32 reference already exposes; kr232 loses locals only if it adopts display
names without the consumer fix.

**Proposed change (coupled).**

1. krcompiler: emit the display name in the `.debug_function` directive (G2 step
   1) using `function->debug_name` with the KR32 fallback.
2. krcompiler: emit the function-scope K record (G6 step 3) whose `function`
   field is the **symbol**, not the display name, and whose range starts at the
   function's begin label. ADR 0030 says "anything that resolves must keep using
   the symbol", and KDBG's `Function` record has only name/start/end/frame/type
   (`krcompiler/include/krc/debug/kdbg.h:34-40`), so the K record is the only
   place the symbol can ride along without a format change.
3. krdbg: the uncommitted consumer change already observed in the working tree
   does exactly the required lookup — `assign_function_symbols` in
   `src/kdbg/kdbg.cpp` derives `FunctionInfo.symbol` from a parentless K scope
   whose range starts at the function and whose `function` names the variable
   records, and `locals()`/`make_view` use `identity()`
   (`git diff src/kdbg/kdbg.cpp src/core/session.cpp` at audit end). With that
   in place no KDBG format addition is needed: kr216/kr232 keep `name == symbol`
   today, and after this fix they have `name` = display, `symbol` = the K
   name. Verify the K `scope.function` for kr232 is exactly the V record's
   `function` string, or the derivation silently falls back to the display name
   and locals disappear again.
4. If the K record cannot be emitted for a function (synthetic with no code
   range), keep its F name equal to its symbol (there is no display name for
   synthetic functions anyway).

Fallback if the K-based derivation is rejected: keep F.name = symbol (no display
names) and record this as known debt — it is better than shipping display names
that erase every C++ frame's locals.

## G4 — FIX-BEFORE — Duplicate and stale F/V records across translation units

**What happens.** Every object's bridge carries F/V records for the functions it
emits. Weak functions (C++ inline/member/virtual functions, destructors) are
emitted by several TUs, and the linker keeps the winning definition's address for
the symbol while retaining every object's bridge (only S/L ids are renumbered —
`krcompiler/src/linker/linker.cpp:123-196`); `attach_kdbg` resolves each F's
start label through a flat name→address map (`linker.cpp:758-761, 839-850`) and
never deduplicates.

**Evidence.**

* Rebuilt image: 8 names × 5 records each (`Computer_dtor`,
  `LibraryComputer_dtor`, `LongRangeScanner_dtor`, `PhaserBank_dtor`,
  `Scanner_dtor`, `ShortRangeScanner_dtor`, `TorpedoTube_dtor`,
  `WeaponSystem_dtor`). Four records per name are identical
  (`Computer_dtor 0x5fe0-0x5ff4`); one is stale: `Computer_dtor
  0x5fe0-0x98dc`, ending at another object's end label.
* Same image: 34 duplicate V records; `Computer_dtor.this` ×5 etc.
* `t5a+t5b` (both define `inline int shared_inline`): kr232 dumps two F records,
  `shared_inline 0x200-0x231` (correct, winning object) and
  `shared_inline 0x200-0x325` (stale end from the losing object); variables
  `shared_inline.x`/`.scaled` are duplicated. KR32 with the same input keeps two
  records but both resolve to the same range (`0x10000-0x10058`), so the damage
  is smaller there.
* Internal functions with the same source name in two TUs (`insideGalaxy` in
  scanner.cpp and weapons.cpp) are not records today (G2), but the label-name
  approach in G2's proposed fix would produce two Fs whose start label resolves
  to the first object unless the labels are per-object — hence the hashed-label
  part of that fix.

**Impact on krdbg.**

* *Params/locals*: `locals()` lists every variable whose `function` matches the
  frame's function name, so a stop in a destructor shows `this` five times.
* *Function attribution*: a stale span that starts at a symbol address and ends
  far past it can cover other functions' address ranges; `find_function` picks
  the innermost range by latest begin (`debug_info_util.cpp:154-173`), so live
  code is usually correct, but dead copies and gaps are attributed to the stale
  name, and `LineInfo.function` (computed in `kdbg.cpp:126-134`) follows that
  attribution.
* *Image size*: repeated records inflate the KDBG payload.

**Proposed change.**

1. Root cause: unique per-object F labels (G2 fix step 2) so a losing object's F
   range stays inside its own (dead) code instead of pointing at the winner's
   symbol.
2. `krcompiler/src/linker/linker.cpp` `attach_kdbg`: after the parse loop
   (line 971, before the `erase_if` at 979) deduplicate `kdbg.functions` by
   `(name, start)` keeping the record with the smallest end, and drop records
   with `end <= start`; deduplicate `kdbg.variables` and `kdbg.variable_info` by
   `(function, name)` keeping the first (link order = object order, and the
   first definition wins for weak symbols). Optionally extend the `symbols` map
   built at 758-761 to carry `LinkedSymbol::size` and bound each function's end
   by `start + size` when the symbol table has a size — that removes the stale
   `0x5fe0-0x98dc` span exactly.

## G5 — FIX-BEFORE — Static/internal globals missing from G records

**What the code does.** The backend emits `.debug_global <name> <type> <size>`
for every non-external global (`kr232_backend.cpp:1494-1513`; `.globl` only when
`!internal`, 1503-1506). The assembler stores those in `global_records`
(162-171) but emits a `G` record only for data labels that are also in `globals`
(571-585, filter at 572). Internal data labels have no `.globl`, so the record is
dropped.

**Evidence.**

* `t1.c`: `symbol g_exported`, `symbol g_point`, `symbol __static_0_counter`;
  `static int g_internal` is absent even though the assembly contains
  `.debug_global g_internal i32 4` (line 13 of `t1-O0.s`). KR32 from the same
  source: `symbol g_internal 0x10234 size=4 type=i32`.
* Rebuilt image: 31 symbols, all `.globl`/`.weak` data (`_ZTV*`, `_ZTI*`,
  `_ZTS*`, `stdin`, `stdout`, `stderr`, `krc_errno`,
  `__krc_current_exception`, `__static_0_storage`, `__static_1_result`). Every
  file-scope static global is missing: `format_buffer`, `format_limit`,
  `heap_next`, `random_state`, `standard_output`, `navigationDx`, `weaponDy`,
  `kRegionSuffixes` are each `symbol … in symbols=False` while their source
  declares them `static`.
* Hand-written `t7.s` repeats it: `.debug_global hand_data` + `.globl` produces a
  symbol; `.debug_global internal_data` without `.globl` produces none.

**Impact on krdbg.** `globals()` is built from `DebugInfo::symbols` with
`size != 0` + absolute locations (`src/kdbg/kdbg.cpp:189-217`) and
`session.cpp:671-685`; a missing G record means the object never appears in the
Globals window, cannot be inspected or watched, and member expansion is
impossible for it. Static objects are the majority of `libc_full.c`'s data.

**Proposed change.** `krcompiler/src/asm/kr232_assembler.cpp`, G emission loop
571-585: emit a `G` record for every data-section label present in
`global_records`, regardless of `globals.count`; keep the existing branch for
labels without a `.debug_global` (size from `symbol_sizes`). The backend needs no
change. krdbg then surfaces them automatically (there is no linkage field in
KDBG, so a static global appears as a normal global — which is what a debugger
should show). Static *locals* are a separate naming issue: the frontend mangles
them to `__static_0_storage` (which is `.globl`, so it already appears) and
nothing records the source name; showing the source name needs a W `static`
record (see G6) or a name map, and is best deferred with G6.

## G6 — FIX-BEFORE — V/K/W: temporaries, parameters, kinds, scopes

**What the code does.** The backend emits `.debug_var` for **every** named
`Alloca` (`kr232_backend.cpp:1767-1788`), stripping only a trailing `.addr`
(1777-1782); parameters are not emitted as a separate group, there is no `.debug`
record for a scope, and the assembler passes V through unchanged
(`kr232_assembler.cpp:146-151, 586-588`). `kdbg` therefore reports
`variables=1408, varinfo=0, scopes=0` for the rebuilt image.

**Evidence.**

* *Temporaries.* `t9.cpp -O0` produces
  `.debug_var scopes argument.copy -80 Vec 8`,
  `.debug_var scopes argument.copy.22 -88 Vec 8`,
  `.debug_var scopes outside.addr.4 -56 i32 4` — compiler temporaries which the
  Variables window lists as user variables because `locals()` has no name filter.
  KR32 from the same source has no such records: it only accepts alloca names
  ending in `.addr` and excludes parameter names
  (`kr32_backend.cpp:1545-1571`).
* *Parameters and `this`.* `t1.c`: `variable helper.a`, `helper.b`,
  `__krc_user_main.local`, `__krc_user_main.p` — no W records, so
  `kdbg.cpp:169-185` falls back to `VariableLocal` for all of them. `t3.cpp`:
  `Widget_get.this`, `Widget_get.k` are parameters but only `this`'s type
  (`ptr<Widget>`) hints at it. KR32 from the same source emits
  `varinfo helper.a kind=param scope=helper` etc. (`kr32_backend.cpp:1535-1539`).
* *Scopes.* `t9.cpp` has two nested blocks each declaring `int outside`; the V
  records are `scopes.outside` (offset -52) and `scopes.outside.addr.4`
  (-56) — no scope information distinguishes them, and at `-O1` the second
  record is gone entirely (see G7). KR32 emits
  `scope scopes:scopes parent=- 0x1026c-0x10450` and W `scope=` fields
  (`kr32_backend.cpp:1511-1515, 1538-1539, 1583-1584`); krdbg already consumes
  the W fields (`kdbg.cpp:150-185`) but has no K translation (see "Consumer-side
  gaps").
* *Hand-written `.s`.* `t8.s` shows the assembler half is usable:
  `.debug_var hand_fn2 x -4 i32 4` yields `variable hand_fn2.x`; the type is
  unresolved only because the file also needs `.debug_type T i32 4` (types=0).

**Impact on krdbg.**

* The Variables window is one flat `LOCALS` list
  (`src/ui/surface.cpp:1030-1037`), built from `session->locals()`. For kr232 it
  contains compiler temporaries (`argument.copy`, `outside.addr.4`), parameters
  and `this` with no label, and no indication of which lexical block a name
  belongs to. Shadowed names are ambiguous (`outside` twice at `-O0`).
* `VariableParam`/`VariableStatic` are never set for kr232, so any future
  param/static filtering or "matches the source declaration" logic has nothing
  to use. (No current UI code consumes those flags — `grep VariableParam
  src/` only finds `fake_debug_info.cpp` and `kdbg.cpp` — which is why this is
  FIX-BEFORE rather than BLOCKS.)

**Proposed change.**

1. `krcompiler/src/backend/kr232_backend.cpp`, V loop 1767-1788: only emit a V
   for a source variable: alloca name ends in `.addr` (also accept the
   disambiguation form `.addr.<digits>` — see G7), strip the `.addr`/`.addr.N`
   segment for the display name, and skip any name equal to a parameter. Emit
   parameters as V+W from `slot_of` with offsets `-(4 + 4*slot)` and kind
   `param`, matching KR32's parameter loop (`kr32_backend.cpp:1516-1540`).
2. Emit a W for each source local: `.debug_varinfo <function> <name> local
   <scope> 0x0`, where `<scope>` is the function name (krdbg maps it through
   `kdbg.cpp:150-185`).
3. Emit the function scope K record: the assembler already knows the function
   labels after G2's fix, so add `.debug_scope <symbol> <begin_label>
   <end_label>` parsing in `kr232_assembler.cpp` (near 146-171) emitting `K
   <symbol> <symbol> - <begin> <end>` before the V records, matching
   `kr32_backend.cpp:1511-1515`. The `function` field must be the **symbol**
   (what V records name), not the display name: that is how krdbg's consumer
   change recovers `FunctionInfo::symbol` for G3. Scope *nesting* (blocks) can
   come later; the function scope is what W references.
4. The V/W function field keeps using the symbol (as today), so krdbg's
   identity-based lookup works; do not switch V records to the display name.

## G7 — FIX-BEFORE — Promoted locals disappear; no P/Location support

**The maintainer requirement.** A variable promoted to a register must stay
listed in the Variables window, with a correct value over its live range. The
KR32 reference approach in the format is a Location (`P`) record over a
label-resolved half-open range (`symbols.md:99`); krc has not adopted it —
`docs/optimize.md:80-84` records that KDBG v1 cannot express a register location,
that kr32 extended its format with P, and that the compiler must either keep
named locals in their slots or take a KDBG change.

**Measured state.**

* kr232 code generation has **no register promotion of its own**: every IR value
  gets a frame slot, and `kr232_backend.cpp` never calls `allocate_registers`
  (no occurrence; the only caller in the tree is `kr32_backend.cpp:1648`). Code
  at `-O0`/`-O1` therefore has a frame home for every recorded local, and the V
  offset is correct.
* The loss is at the IR level. `alloca_promotion` is called with
  `promote_recorded = options.optimize >= 2` (`src/toolchain/toolchain.cpp:685-687`),
  and `is_recorded_variable` only recognises a name ending exactly in `.addr`
  (`src/ir/passes.cpp:326-331`). `IRBuilder::make_name` disambiguates a repeated
  name by appending `.<counter>` (`src/ir/ir.cpp:810-822`), so a shadowed source
  variable is named `outside.addr.4`: it is **not** recognised as recorded and is
  promoted even at the default `-O1`. At `-O2` `promote_recorded` becomes true
  and every recorded alloca is a candidate.
* No P record is emitted by any backend (`grep '"P "' src/backend` finds none).
  At HEAD krdbg's translator also never read `KdbgImage::locations`
  (`src/kdbg/kdbg.cpp:62-217` has no locations loop), although
  `variables.cpp:53-77` implements `LocationKind::Register` and the fake
  fixtures use it (`fake_debug_info.cpp:86,127,157`); the uncommitted consumer
  change in the working tree adds that translation (see "Consumer-side state").

**Evidence (image V-record counts).**

| input | `-O0` | `-O1` (default) | `-O2` |
|---|---|---|---|
| `t1.c` | 5 | 5 | 1 (`__krc_user_main.p`; `local`, `helper.a/b/sum` gone) |
| `t9.cpp` | 14 | 13 (second `outside` gone: promoted because it was named `outside.addr.4`) | 8 (`scopes.seed`, `total`, `outside` gone; only aggregates + temps remain) |

This is exactly what the user sees: at `-O2`, stepping into `main` lists only
the struct local; the scalar `local` is live in a register and invisible, not
shown as `?`. At `-O1`, a shadowed local can disappear while its sibling with the
same source name remains, so the window is also inconsistent.

**Impact on krdbg.** `locals()` lists only V-backed variables; a promoted local
has no record, so it silently disappears from the Variables window (and any
golden or test that counts locals changes). krdbg's location model can express a
register only for the whole frame (`VariableInfo.location` is one `Location`,
`debug_info.h:92-101`), so even if P records appeared the translator would have
to pick the range in effect at the PC. There is no current way to display it as
"optimized out" either.

**Proposed change — immediate (keep named locals in slots).**

1. `krcompiler/src/ir/passes.cpp:326-331`: make `is_recorded_variable` accept
   `<name>.addr` and `<name>.addr.<digits>` (i.e. a `.addr` segment at the end,
   optionally followed by a numeric disambiguator), so shadowed source variables
   are protected at every level. (`make_name`'s counter is the only suffix that
   appears today; if it ever appends a non-numeric segment the check must be
   revisited.)
2. `krcompiler/src/toolchain/toolchain.cpp:685-687`: gate promotion of recorded
   allocas on a target capability rather than on the optimisation level alone:
   `ir::alloca_promotion(*module, options.optimize >= 2 && target_has_location_records(id))`,
   with kr232 (and kr32/kr16 until a P/location path lands) false. That keeps
   every named local in its frame slot at `-O2`, which is the conservative half
   of `docs/optimize.md:80-84`'s choice and what the maintainer asked for.
   Document the `-O2` caveat in `docs/optimize.md` and ADR-level docs if this is
   the accepted policy.
3. `krcompiler/src/backend/kr232_backend.cpp:1777-1782`: strip the display name
   from the `.addr` segment (so `outside.addr.4` becomes `outside`, not
   `outside.addr.4`) as part of G6's V filter.

**Proposed change — when promotion is funded (matching KR32's format).**

4. Emit a P record per promoted variable with the labels of its live-range
   start/end (the assembler already resolves line labels; P's range labels must
   be added as symbols the same way as G2's begin/end labels), and emit
   `.debug_location` (new directive) parsed into `P` in
   `kr232_assembler.cpp` (near 146-171) and handled by `attach_kdbg` (already
   implemented, `linker.cpp:884-903`).
5. krdbg: the uncommitted consumer change observed in the working tree already
   provides the model — `LocationInfo` and `DebugInfo::locations`
   (`include/krdbg/debug_info.h`), `location_at()`
   (`src/core/debug_info_util.h`, `src/core/debug_info.cpp`), P translation and
   sorting (`src/kdbg/kdbg.cpp`), range resolution plus `<optimized out>` in
   `make_view` (`src/core/session.cpp`), a resolved-Location
   `read_variable_storage` and `register_label()` (`src/core/variables.cpp`), and
   `VariableSlotUnusable` mapped from W flag bit 2. What is still missing there is
   the surface half: `src/ui/surface.cpp` is untouched in that change, so nothing
   yet renders `VariableView::location_text`/`unavailable`; check that before
   claiming a promoted local is user-visible. On the producer side the W record
   that sets bit 2 and the P range records still have to exist, and the P range
   labels must be per-object symbols (same mechanism as G2).

This gap overlaps `docs/register-promoted-locals.md` (task-9); that document owns
the detailed design. The kr232 audit's finding is that until P/location support
exists, the only correct kr232 behaviour is "keep named locals in slots", and
the `.addr.<digits>` hole in `is_recorded_variable` breaks even the `-O1`
guarantee today. **Treat `-O2` as unsupported for variable inspection on kr232**
(and say so in `docs/debugging-kr232.md` / `docs/optimize.md`) until promotion
either keeps the V record with a Location range or the debugger can show the
value from the register.

## G8 — FIX-BEFORE — Split `-S`/`-c` loses the source identity

**What happens.** `.debug_line` sets only a line number; the assembler's S record
is always its input file (`krcompiler/src/asm/kr232_assembler.cpp:518`,
`emit("S 1 " + std::string(file_name))`). For a direct `krc -c t.c` the input is
the C file, so source id 1 is the C file. For `krc -S t.c` followed by
`krc -c t.kr232.s`, the assembler's input is the generated assembly: the image
names the `.s` file but keeps the C line numbers from the embedded directives.

**Evidence.** `t1-O0.s` reassembled and linked:
`source 0 t1-O0.s` with `line 0x254 line=8`, `0x258 line=11`, … — i.e. C source
lines attributed to `t1-O0.s`. Hand-written assembly is unaffected and correct
(`t4.s`: source line numbers equal `.s` lines; see G1 evidence).

**Impact on krdbg.** Breakpoints and source stepping in the split workflow open
the wrong file (the generated assembly, whose only available lines are the
assembly's, not the C line numbers recorded) or fail to find a source at all.
The direct-compile workflow is correct today, but a makefile that compiles and
assembles in two steps is a normal use of `-S`.

**Proposed change.**

1. Carry the original path in the generated assembly: in
   `krcompiler/src/toolchain/toolchain.cpp`, `compile()`'s kr232 branch
   (734-744) prepend `.debug_file <options.source_name>` when `source_name` is
   non-empty (same shape as the `.target` insertion at 783-788, but the
   assembler consumes this directive rather than stripping it).
2. `krcompiler/src/asm/kr232_assembler.cpp`: parse `.debug_file` next to the
   other debug directives (146-171) and use it in place of `file_name` for the
   `S` record at 518 (and only there — the L records' source id stays 1).
   Hand-written `.s` without the directive keeps today's behaviour.
3. `attach_kdbg`'s source rewriting (`linker.cpp:997-1031`) already handles the
   path/root; no change there.

## G9 — ERRATA — F frame and return type are placeholders

The assembler writes `F <name> <name> <end> 0 0`, so KDBG shows `frame=0
type=0` for every kr232 function (e.g. `function main 0x333-0x349 frame=0
type=0`), while KR32 writes the real frame size and return type
(`kr32_backend.cpp:1510-1516`: `frame=88 type=i32`). `symbols.md:59` defines the
Function record as "name, start, end, frame size, result type", and
`debug_info.h:56-60` documents the frame field as the saved-caller-frame offset.

No current krdbg behaviour depends on `FunctionInfo.frame` for kr232: the kr232
adapter unwinds the hardware ENTER/LEAVE chain itself
(`adapters/kr232/kr232_adapter.cpp:298-337`) and the translator copies the field
but no code reads it (`src/kdbg/kdbg.cpp:118` is the only assignment; `grep
'\.frame' src/ adapters/` finds only kr32_adapter.cpp:89, which is KR32's
unwinder). So the impact is on other KDBG consumers and on correctness of the
format. The `.debug_function` directive in G2 includes `frame` and `ret_type`;
this entry can be closed together with G2.

## G10/G11/G12 — ERRATA (size, column, function source line)

* **G10** — one L record per instruction rather than per statement: `t1` 69
  records for 5 distinct lines (KR32: 38), `t6` 207 (KR32: 101), `t9` 155
  (KR32: 89), rebuilt image 32,307. Correctness is unaffected; the KDBG payload
  roughly doubles. The linker drops only lines outside function ranges; nothing
  coalesces equal consecutive lines. If image size matters, coalesce identical
  consecutive `(line, address-run)` records in `attach_kdbg` or emit a directive
  only when the line changes (the assembler currently pushes a `DebugLine` at
  every instruction, 411-412 etc., because each instruction needs its own
  offset; coalescing can be done at the bridge stage in
  `kr232_assembler.cpp:542-547` by collapsing equal lines whose addresses are
  contiguous).
* **G11** — column is always 0. KR32 writes the real column
  (`add_debug_line`, `kr32_backend.cpp:1285-1297`); `.debug_line` in kr232 has no
  column operand. krdbg's `LineInfo` has no column field (`debug_info.h:44-49`),
  so there is no consumer today; add a second operand to `.debug_line` if a
  consumer ever needs it.
* **G12** — no KDBG function source/line. krdbg sets `FunctionInfo.source = 0`
  and `line = 0` unconditionally (`src/kdbg/kdbg.cpp:116-117`); KDBG's Function
  record has no such field. No consumer reads them today; leave alone.

## Consumer-side state in krdbg (HEAD vs the uncommitted working tree)

At HEAD (`71e4591`, the snapshot the krdbg citations in this document use):

* `src/kdbg/kdbg.cpp` translates `KdbgImage::variable_info` (W) but never
  `KdbgImage::scopes` (K) or `KdbgImage::locations` (P). `VariableInfo.scope` is
  therefore populated only by W `scope=`.
* `locals()` matches variables by function *name* (`session.cpp:654-663`), the
  blocker for display names.
* `FunctionInfo.source`/`line` are never set (see G12).

The uncommitted change in the working tree at the end of the audit
(`include/krdbg/debug_info.h`, `src/core/debug_info.cpp`,
`src/core/debug_info_util.h`, `src/core/session.cpp`,
`src/core/variables.cpp`, `src/core/variables.h`, `src/kdbg/kdbg.cpp`) adds
`LocationInfo`/P translation, `FunctionInfo::symbol` + `identity()` derived from
K scopes, `location_at()`, a resolved-Location `read_variable_storage`, and
`VariableSlotUnusable`/`<optimized out>`. That covers the krdbg half of G3 and
G7; the producer half (K scopes carrying the symbol, W kinds, P ranges) is still
open, and the surface does not yet render the new `VariableView` fields.

Vendored KDBG reader: `third_party/krc-debug/` is copied from KRCompiler
`e04b3b2`. Against current `krcompiler/src/debug/kdbg.cpp` the only difference is
the `.kr216` container recognition in `find_kdbg_section`, so today's kr232
payloads (v1.1 records, type base/element/target, P/K tags) decode correctly; no
copy update is needed for G3's K-based approach or G7's P records, provided the
KDBG tags themselves do not change.

## What is already correct

* **`.s` line records.** Hand-written assembly without directives keeps real
  `.s` line numbers: `t4.s` → `line 0x200 line=5 … 0x20b line=9`; `t7.s` with
  `.debug_line 100` → all records line 100. The assembler's fallback
  (`kr232_assembler.cpp:111-113, 153-157, 411-412`) is right for this input;
  G1's fix must preserve it.
* **Assembler debug directives parse and pass through.** `.debug_type`,
  `.debug_global`, `.debug_var` and `.debug_line` are consumed at
  `kr232_assembler.cpp:146-171`; `t7.s`/`t8.s` prove `.debug_global` → G,
  `.debug_line` → L, `.debug_var` → V (`t8`: `variable hand_fn2.x offset=-4
  size=4`; a `.s` author needs `.debug_type T i32 4` for the type to resolve).
* **Multi-TU source renumbering.** `merge_debug_records`
  (`linker.cpp:123-196`) remaps S ids per object and rewrites L ids; `t5a+t5b`
  and the rebuilt 12-source image both have distinct, correct `source` records
  (e.g. `source 3 examples/startrekng/galaxy.cpp`). This is why per-file line
  attribution works at all after the WIP.
* **Global type/size for external data.** `g_exported i32 4`, `g_point Point 8`
  (`t1`); `g_arr [3_x_i32] size=12`, `g_s S size=24`, `g_u U size=8`,
  `g_ptr ptr<i32> size=4` (`t6`) — identical to KR32.
* **Type records and aggregates.** Struct members with correct offsets/sizes
  (`S`: `name`@0 `[6_x_i8]`, `values`@8 `[3_x_i32]`, `nested`@20 `i32`), array
  types with element/count/size, pointer targets, base-class field on `C`.
  Local aggregates expand through the V size: `t1`'s `Point p` at -28 size 8,
  `t6`'s `S local_s` at -52 size 24, `[4 x i32] local_arr` at -68 size 16.
  `t6.c`'s kr232 and kr32 type dumps are identical in kind, size, member offsets
  and element counts (only record order differs).
* **Local/parameter frame offsets.** `.debug_var` writes the frame displacement
  (`-(4 + 4*(slot+words-1))`, `kr232_backend.cpp:1715-1723, 1783-1786`), and
  krdbg widens it as signed (`kdbg.cpp:163-168`); offsets point at the object's
  lowest byte, matching the generated `mov r?,r13; sub #offset` addressing.
  Values for scalars and aggregates read correctly through
  `variables.cpp:78-83` (this is what makes the existing `nested_frames` and
  `aggregate` fixtures pass).
* **F bounds for external functions** (apart from duplicate weak records): start
  is the function symbol and end is the next text label in the same object, or
  the object's text end for the last function — `t1` `__krc_user_main
  0x254-0x305` is correct, `t7.s` `hand_fn 0x208-0x211` is correct.
* **KDBG v1.1 decoding in krdbg**: the vendored reader supports base/array/ref
  records and Location64/Scope/VarInfo tags (`kSupportedMinor = 1`), so the
  rebuilt image's 130 type records load.
* **G records with type and size for `.globl`/`.weak` data** are correct
  (`g_point Point 8`; weak vtable objects present). The defect is only the
  internal-linkage filter (G5).

## What could not be obtained

* krdbg itself was not executed: there is no `krdbg_demo.exe`/test binary in
  `build/`, and the audit was instructed to write only the doc. All krdbg impact
  statements above are code-path analysis with file:line citations.
* `krc --dump-symbols --json` does not include L records (the text dump does);
  per-line evidence therefore comes from the text dump, not JSON.
* The committed fixtures (`tests/fixtures/kr232/*.kr232`) were built before the
  WIP: `session.kr232` has 343/395 L records beyond their file's last line
  (e.g. `tests/fixtures/c/kr232_session.c` is 12 lines but records reach 47),
  `varinfo=0`, `scopes=0`. They must be regenerated with a post-fix compiler
  before the scenario tests can assert the new line/F/V behaviour;
  `tests/fixtures/kr232/regenerate.ps1` defaults to
  `build/windows-msvc-debug/.../krc.exe`, which is the *installed* revision
  `80960f0` and does not contain the WIP either.
* `build/startrekng-kr232/startrekng_script.kr232` was built with the installed
  prefix compiler (`types=0 symbols=0`); a rebuilt image was used for the
  current-state numbers. The command recipe is in "How the evidence was
  gathered" and in `scripts/build-startrekng-kr232.ps1`.
