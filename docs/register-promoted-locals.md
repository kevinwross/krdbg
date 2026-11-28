# Register-promoted locals and the Variables window

**Status:** audit and design proposal, 2026-11-27. No code was changed to write it.
**Question.** KR32 and its compilers coped with register promotion so a promoted local
still appears in the debugger's Variables window. How did that work end to end, what do
kr232 and krdbg do today, and what is the kr232 equivalent?

**Answer in one paragraph.** The coping is not a `P` record today: krc refuses to
promote a *named source variable* at `-O0`/`-O1`, so the local keeps a frame home and
its `V`/`W` records stay true. `P` (register location over a half-open range) exists in
the format and in the linker, is consumed by the KR32 VM, and has **no producer** in
krc; krdbg **decodes but discards** it. kr232 inherits the same `-O1` protection but
breaks three other links in the chain: its text peepholes delete stores to named
locals, it lists compiler temporaries as locals, and it emits no function record for
static functions; at `-O2` it promotes named locals and every variable disappears.
The fix is to make the home-slot promise true on kr232 and add the missing `W`/`K`
records (matching KR32), then add the range-based location producer and krdbg
translation as the forward path for code that genuinely keeps a local in a register.

Repository roots used below (all absolute on this host):

- debugger: `C:\Users\kevin\repos\krdbg` — paths below are relative to it.
- compiler/linker: `C:\Users\kevin\repos\krcompiler` — paths are written `krcompiler/<path>`.
- kr32 VM and its historical compiler's output format: `C:\Users\kevin\repos\kr32` — paths are written `kr32/<path>`.

The empirical evidence in §6 was produced with the installed KRCompiler package
(`krc 0.2.0, revision 80960f0`). That revision is an ancestor of the krcompiler tree's
`HEAD` (`a9e22a0`); the later commits add kr232 type records (`ff72fce`) and a
`.debug_line` bridge (uncommitted working-tree change) but do not touch the promotion
or `V`-record paths relied on here. §6 states this caveat where it matters.

---

## 1. KR32's mechanism, end to end

There are two mechanisms, and only one of them runs today. Both are worth keeping
straight because the kr232 question needs the first now and may need the second later.

### 1.1 What runs today: named locals keep a frame home

1. **Every source variable is an `alloca`.** IRGen creates one named
   `<name>.addr` per parameter and per local and binds the source name to it
   (`krcompiler/src/irgen/c_irgen.cpp:116`, `:1663-1666`). Every read and write of
   the variable is a `load`/`store` through that pointer.
2. **The promotion pass refuses recorded variables.** `is_recorded_variable` matches
   an alloca whose name ends `.addr` (`krcompiler/src/ir/passes.cpp:326-331`);
   `is_promotable(..., promote_recorded)` returns false for it unless
   `promote_recorded` is true (`passes.cpp:333-341`). The driver passes
   `promote_recorded = (options.optimize >= 2)` (`krcompiler/src/toolchain/toolchain.cpp:686`),
   so at `-O0` and `-O1` a named local's alloca, loads and stores survive the
   optimiser.
3. **Dead-store elimination also refuses them.** `eliminate_dead_alloca_stores`
   skips any alloca whose name ends `.addr` (`passes.cpp:588-592`), so a store to a
   named local is never removed by the IR optimiser even when the program never reads
   it.
4. **Load forwarding may remove loads, never stores.** `forward_alloca_loads` replaces
   loads with the value last stored and erases only the load instructions
   (`passes.cpp:373-569`, erasure at `:553-566`). The store that establishes the
   home slot's value remains in the IR and is emitted.
5. **Register allocation covers expression values only.** At `-O1` the KR32 backend
   runs its allocator over IR values whose definition and every use are in one block
   with no call between them, drawing on `R12`-`R15`
   (`krcompiler/src/backend/kr32_backend.cpp:1629-1676`).
   `keep_local_allocations` iterates instructions and explicitly skips `Alloca` and
   `Phi` (`krcompiler/src/backend/register_allocator.cpp:278-281`), and parameters are
   never in the kept set; so `allocated_registers_` never holds a source variable.
   Note the comment at `krcompiler/src/backend/register_allocator.h:3-10` still says
   "NOT WIRED IN"; that is stale — `Kr32Backend::compute_allocation` calls
   `allocate_registers` at `kr32_backend.cpp:1648` (ADR 0049 wired it). The rest of
   that header (`:30-57`) describes the real contract.
6. **The layout always reserves a slot for an alloca.** `layout_function` adds every
   alloca to `alloca_offsets` unconditionally (`kr32_backend.cpp:332-340`); a slot is
   omitted only for a non-alloca value the allocator kept in a register
   (`kr32_backend.cpp:341-359`). The load/store paths use the alloca's `LEA`/frame
   slot before they ever consult `allocated_registers_` (`kr32_backend.cpp:431-470`,
   `:473-505`).
7. **The debug records are keyed on the alloca.** The backend emits `F` (function,
   display name), `K` (function scope), and for every parameter a `V` frame offset
   plus a `W param` record; for every `.addr` alloca a `V` frame offset plus a
   `W local` record, and **only** for `.addr` allocas
   (`kr32_backend.cpp:1498-1585`; parameter path `:1517-1539`, local path
   `:1545-1585`, temporary exclusion `:1559-1563`). Compiler temporaries such as
   `call.temp`, `operator.temp`, `subscript.this` are deliberately not recorded, so a
   later pass may promote one without leaving a record pointing at a dead slot
   (`krcompiler/docs/symbols.md:195-199`).

The net effect is the property the maintainer remembers: at `-O0`/`-O1` a named local
is *both* cached in a register where profitable (as an SSA expression value) and
mirrored to its frame slot at every assignment, and the debug record describes the
slot. A debugger reading the slot is right. Measured on
`int compute(int a,int b){int x=a+b;x=x*2;return x;}` at `-O1`: KDBG has
`variables=3` (`a` offset 8, `b` offset 12, `x` offset 32), `varinfo=3`, `scopes=1`,
`locations=0`; the assembly stores `x` to `[R7 + 32]` at both assignments (§6).

**Range semantics do not exist in this mechanism.** The `V` record is a promise for the
whole function; the pass set that would need it is exactly what is refused. This is the
state ADR 0052 calls "not wrong today" (`krcompiler/docs/decisions/0052-kdbg-variable-scope-ranges.md:30-35`).

### 1.2 The `P` record: format and historical mechanism, no producer

`P <function> <name> <register> <start_label> <end_label>` is the bridge spelling for
"this variable is in `R<n>`/`F<n>` over `[start, end)`", with labels resolved through the
linked symbol table (`krcompiler/docs/symbols.md:97-107`,
`kr32/docs/debug.md:18`). The range is **half-open**: the register is in effect at
`start` and not at `end`. The historical KR32 compiler emitted it from its promotion
passes; the KR32 reader prefers the register inside the range and falls back to
`R7 + offset` outside it (`kr32/docs/debug.md:40-51`). Whole-function promotions take
their range from the function's own `F` labels; span promotions name the labels that
already bracket the interval, so their range may omit a few instructions at each end.
A whole-function range covers the stretch before the first assignment, so an
unassigned local reads the register's stale contents rather than "unavailable" — the
same way a frame slot reads before its first store; narrowing to the first store is
deferred because it would make the record depend on a moving line index
(`kr32/docs/debug.md:43-51`, `kr32/implementation.md:4338-4344`).

KR32's reader is the reference implementation for the consumer half:

- parse `P` in `kr32/src/debug/debug.cpp:183-200`,
- resolve a variable's location at a PC in `Session::location_at`
  (`debug.cpp:717-727`, half-open test at `:722`),
- `read_variable` prefers that register (GPR or FPR low word) and falls back to the
  frame slot otherwise (`debug.cpp:729-762`),
- the panel shows `register: R9` instead of an address and expands members through the
  register (`kr32/src/ui/debugger_ui.cpp:2380-2400`, `kr32/docs/ui.md:233-235`).

Production was fragile and was learned the hard way: a pass that inserts save
instructions moves later label indices, so ranges are anchored on labels that already
exist and `P` text is appended once after every pass
(`kr32/implementation.md:4264-4272`, `:4283-4304`); label/name lookups by code label
mismatched qualified names so **C++ never produced a valid `P` record**
(`kr32/implementation.md:4711-4726`). The range practice in §1.2 is the contract to
copy, not the implementation route.

In krc, the bridge parser and KDBG encoding exist and are frozen into v1.1:

- linker parses `P` and resolves both labels to addresses
  (`krcompiler/src/linker/linker.cpp:884-903`), and parses `K` (`:904-920`) and `W`
  (`:921-937`);
- KDBG v1.1 carries `Location` tag 9 / `Location64` tag 18 in `.kdbg_loc`
  (`krcompiler/include/krc/debug/kdbg.h:49-58`, `:114-136`;
  `krcompiler/docs/symbols.md:114-135`);
- **no krc path emits one**: `kdbg.h:49-50` says so, and ADR 0052 records
  "The compiler still does not emit `P`" (`0052-...md:22-23`), as does the v1.1
  implementation log (`krcompiler/docs/kdbg-v1-1-log.md:57-104`, `:128-134`,
  `:199-200`). The producer is deferred to a separate promotion change.

### 1.3 How the linker maps it

`attach_kdbg` reads NUL-terminated bridge records out of `.debug_info` and builds the
KDBG image (`linker.cpp:742-1034`). `V` becomes `KdbgVariable` (`:860-864`); `P`
becomes `KdbgLocation` after both labels resolve through the link symbol table, with
`fp` set from the `F` prefix and `reg` parsed from the number that follows it
(`:884-903`); `W` becomes `KdbgVarInfo` (`:921-937`). So a `P` record that reaches the
linker already has absolute, half-open start/end addresses in the image's load space —
the same space as function ranges and line records. Nothing else in the linker has to
change for a producer to land.

### 1.4 How krdbg consumes it

krdbg's vendored decoder decodes the record: `Record::Location` and
`Record::Location64` populate `KdbgImage.locations`
(`third_party/krc-debug/src/kdbg.cpp:628-651`), and both dumpers print it (`:797`,
`:849`, `:906-915`). Since the Track B consumer landed (2026-11-27),
`translate()` in `src/kdbg/kdbg.cpp` copies each record into
`DebugInfo::locations` (fp selects `fpr`, otherwise `gpr`, index is `reg`), sorted
by `(function, name, start)`. `DebugSession::make_view` resolves the range at the
frame's PC: inside it frame 0 reads the register and a caller frame reports the
value unavailable, outside every range the `V` home is read unless
`VariableSlotUnusable` is set. `VariableView` carries the resolved location and
`members()` expands through it. A v1 payload has no records, so it behaves as
before.

---

## 2. Current kr232 + krdbg state

### 2.1 Does kr232 promote locals?

**At `-O0` and `-O1`: no named local is register-resident, and no `P` record is
emitted.** kr232 runs the same IR pipeline as every target
(`krcompiler/src/toolchain/toolchain.cpp:661-710`): constant folding, load forwarding,
dead alloca-store elimination, `alloca_promotion(promote_recorded = optimize >= 2)`,
copy propagation, DCE, empty-block elimination. The `.addr` refusals of §1.1 apply
unchanged, so named locals keep their slots. E6 (`promote_cross_block_allocas`) is
skipped for kr232 because its backend has no `phi` lowering
(`toolchain.cpp:670-681`, ADR 0060:29-31).

The kr232 backend has **no register allocator at all** (`src/backend/kr232_backend.cpp`
has no call to `allocate_registers`), and its emitter materialises every operand from
its slot into scratch registers on demand (scratch roles `R6`/`R7`/`R8` at
`kr232_backend.cpp:118-124`, `materialize` at `:1831+`). Its frame-slot colouring is
the closest thing to slot promotion and it **excludes allocas by construction**
(`plan_one_word_value_slots`, `kr232_backend.cpp:196-288`, comment at `:196-199`;
allocas get their own non-shared slots at `:1700-1709`). So a named local's frame
offset is stable and never aliases another value.

**At `-O2`: yes, named locals are promoted, and every variable disappears.** `-O2`
makes `promote_recorded` true (`toolchain.cpp:686`; level mapping `:158-166`), so
`alloca_promotion` deletes `<name>.addr`, and the backend's `V`-record loop keys on
surviving allocas (`kr232_backend.cpp:1767-1787`). Unlike the KR32 backend, kr232 has
no separate parameter loop, so parameters disappear too. There is no `P` record and no
"optimized out" entry, so the Variables window is empty. Measured in §6:
`variables=3` at `-O1`, `variables=0` at `-O2` on the same source.

### 2.2 What kr232 emits today

- `V` records only, from `.debug_var <function> <name> <negative offset> <type> <size>`
  directives (`kr232_backend.cpp:1763-1787`; assembler forwards them at
  `src/asm/kr232_assembler.cpp:146-151`, `:586-588`).
- **No `W` records, no `K` scopes, no `P` locations.** The assembler knows only
  `.debug_var`, `.debug_line`, `.debug_type` and `.debug_global`
  (`kr232_assembler.cpp:146-171`); the committed type-record work (`ff72fce`) added
  `T/C/M/A/R` but no `K/W`.
- **Temporaries are not filtered.** The loop records every named alloca, only stripping
  a `.addr` suffix when present (`kr232_backend.cpp:1777-1786`). C++ object
  temporaries therefore appear as locals: measured `f.temporary.record` and
  `f.temporary.this` in a one-line C++ function, where KR32 emits only the three
  parameters (§6).
- **Function records exist only for global symbols.** The `F` records are synthesised
  by the assembler from text labels, and the loop skips any label not in `.globl`/
  `.weak` (`kr232_assembler.cpp:548-570`, skip at `:563-565`). A `static` function
  therefore has `V` records but no `F` record. Measured: only `entry` in the function
  table while `variable helper.a` and `variable helper.doubled` exist (§6). The F
  record also carries `0 0` for frame size and result type (`:569`), and no display
  name (the backend uses the symbol, `kr232_backend.cpp:1579`).

### 2.3 The kr232-specific hole that breaks the home-slot promise

The `-O1` text peepholes include two passes that delete frame stores using machine
liveness (`kr232_backend.cpp:2647-2665`):

- `eliminate_local_dead_frame_stores` (`src/backend/kr232_cfg.cpp:416-505`) deletes an
  earlier store when a later store to the same slot appears before any load.
- `eliminate_dead_frame_stores` (`kr232_cfg.cpp:507-564`) deletes a store whose slot is
  never loaded again.

Their liveness model (`analyze_slots`, `kr232_cfg.cpp:258-306`) knows frame slots and
registers but knows nothing about `.debug_var` records: a debug record makes a slot
observable to a debugger but not to this pass. Measured on
`int fixed(int a){int unused=a+1;return a;}` at `-O1`: the assembly still emits
`.debug_var fixed unused -16 0 4` but contains no store to `[r13-16]`, so a breakpoint
in `fixed` reads stale or zero bytes for a local the Variables window lists. The KR32
backend keeps the corresponding `STORE R14, [R7 + 20]` because its IR dead-store pass
refuses `.addr` allocas and it has no equivalent text pass (§6). This is the most
concrete "promotion coping" regression in the current tree: at the default level,
kr232 can list a local with the wrong value.

The other slot-moving passes are safe for `V` records: `reuse_frame_addresses*` and
`fold_frame_addresses*` reuse the *address register*, not the slot, and
`forward_frame_loads*` removes loads (the store remains), so they cannot invalidate a
slot. The two dead-store passes are the ones that must learn about recorded slots.

### 2.4 Does krdbg apply Location records?

**Yes.** The consumer side described in §3.4 is implemented: `src/kdbg/kdbg.cpp`
translates `image.locations` into `DebugInfo::locations`,
`detail::location_at` selects the first half-open range containing the PC, and
`DebugSession::make_view` reads the register inside it. The pieces that were
already there stay in place:

- `read_variable_storage` handles `LocationKind::Register` by calling
  `target.read_value(group, index, ...)` and slicing to the variable's size
  (`src/core/variables.cpp`, register case).
- The fake target path exercises register values end to end: `argc` in `gpr:0` and an
  8-byte `held` in `core:5` are declared in `src/core/fake_debug_info.cpp`, and
  `tests/test_session.cpp` reads both values through `locals(0)`;
  `tests/test_variables.cpp` pins the storage rules and
  `tests/test_debug_info.cpp` asserts a register location exists.
- The kr232 adapter exposes register groups `gpr` 0-15 and `fpr` 0-3
  (`adapters/kr232/kr232_adapter.cpp:183-197`, `:408-435`), which map exactly onto
  `KdbgLocation.reg`/`fp`.

No backend emits a `Location` record yet, so the path is proven by synthetic
`DebugInfo` and hand-built KDBG payloads (`tests/test_session.cpp`,
`tests/test_kdbg.cpp`).

### 2.5 What a user sees today

| Situation (kr232 unless stated) | Variables window |
|---|---|
| `-O1`, local assigned and read later | Listed with the correct value (the home-slot path; `nested_frames.kr232` asserts `x` at frame offset `-12` and value 3, `tests/test_kr232_adapter.cpp:606-632`, `:500-521`) |
| `-O1`, local assigned but never read by the program | Listed, but reads stale/zero: its last store was deleted by `eliminate_dead_frame_stores` (§2.3) |
| `-O1`, C++ object temporaries | Listed as extra locals: `temporary.record`, `temporary.this` (§2.2) |
| `-O1`, stop inside a `static` function | **Nothing**: no `F` record, so `DebugSession::locals` finds no function (`src/core/session.cpp:645-669`) |
| `-O2` | **Nothing**: named locals and parameters are promoted away and no `P` record exists yet; with a `W` flag bit 2 the row would read `<optimized out>` |
| Any level, a `Location` record present | Applied inside its range: the row reads the register, shows `register: R9`/`register: F2`, and members expand through it |
| Any level, outside every range with the slot flagged unusable | `<optimized out>` rather than a stale slot read |
| KR32/kr16 C++ member functions | Listed: `locals` matches `variable.function` against the function identity recovered from the F/K pair, so `V`'s mangled symbol resolves; the call stack and the rows show the display name |

The Variables panel shows `name = value` from `VariableView`
(`src/ui/surface.cpp`); a live-range register appends `register: R9`, a caller
frame's register-resident value shows `?`, and an unusable slot shows
`<optimized out>`.

---

## 3. Design to match KR32 for kr232

### 3.1 Decision and why

**Copy KR32's home-slot + filtering mechanism now; add range-based locations as the
forward path, with the krdbg consumer implemented before any kr232 producer.** Do not
try to make kr232 emit `P` today: it would be a false record. kr232 has no
allocated-register concept — operands are materialised from slots into scratch `R6`/
`R7`/`R8` per use (`kr232_backend.cpp:118-124`, `:1831+`) — so there is no register
that holds a given local over an interval. The only true register location available
without a codegen change is "a parameter is in its ABI register between entry and the
prologue store", which expires before the first source statement; the KDBG v1.1 log
already examined and rejected that as useless
(`krcompiler/docs/kdbg-v1-1-log.md:80-87`). A range producer needs a backend that pins
a value to a register, i.e. the machine-IR/allocator route (ADR 0051) rather than
kr232's current text emitter.

That gives two tracks whose ordering is enforced by dependencies:

- **Track A (required, no format change):** make the `-O0`/`-O1` home-slot promise true
  for kr232, and give its `V` records the same identification data KR32's have
  (`W`/`K`, no temporaries, an `F` for every function). This satisfies the maintainer's
  requirement for every build level that does not promote a named local, and it fixes
  the one case where kr232 currently shows a wrong value.
- **Track B (forward, format already frozen):** implement the krdbg consumer for
  `Location` records now, and the kr232 `P` producer only when a kr232 codegen change
  actually keeps a local in a register. Until then, kr232 must not promote `.addr`
  allocas at any level, or the Variables window silently empties at `-O2`.

### 3.2 Track A — producer changes in krc (kr232)

1. **Filter compiler temporaries out of `V`.** In the kr232 backend's `V` loop, require
   the alloca name to end `.addr` and strip it, exactly as the KR32 backend does
   (`kr32_backend.cpp:1559-1571`); drop the "strip if present" fallback at
   `kr232_backend.cpp:1777-1786`. This removes `temporary.*`, `call.temp`,
   `operator.temp`, `subscript.this` from the panel and matches
   `krcompiler/docs/symbols.md:195-199`. Parameter allocas are `<name>.addr` too, so
   they keep their records; skip the duplicate when a parameter shares the name, as
   kr32 does (`kr32_backend.cpp:1569-1571`).
2. **Emit `W` and `K`.** Add `.debug_varinfo <function> <name> <param|local|static>
   <scope> <flags>` and `.debug_scope <function> <name> <parent> <start_label>
   <end_label>` directives to the kr232 assembler, forwarding them as the existing
   `W`/`K` bridge letters (the linker already parses both,
   `linker.cpp:904-937`). The backend emits the function-scope `K` and one `W` per
   variable, using the same function name in `F`, `K`, `W` and `V` (see item 4).
   Formatting is the KR32 backend's (`kr32_backend.cpp:1511-1515`, `:1538-1539`,
   `:1583-1584`).
3. **Protect recorded slots from the dead-store peepholes.** Compute the set of frame
   offsets named by `.debug_var` (the backend already knows them from `slot_of` and
   `alloca_offset`), pass it into `eliminate_local_dead_frame_stores` and
   `eliminate_dead_frame_stores`, and treat such a slot as live at every point: never
   delete a store to it, including the earlier-store case. The rule belongs in the
   pass contract in `kr232_cfg.h`, with a comment that a debug record makes a slot
   observable to a debugger. Alternative shapes rejected: running the passes before
   the debug records exist changes nothing (the pass still cannot see them); marking
   only the last store keeps the earlier-store wrong-value case.
4. **Give every function an `F` record.** Add `.debug_function <display_name>
   <symbol> <end_label> <frame_size> <result_type>` or let the backend emit the `F`
   record through the assembler bridge and stop synthesising `F` from `.globl` labels
   (`kr232_assembler.cpp:548-570`). The backend knows the frame size
   (`align_frame(next_slot * 4u)`, `kr232_backend.cpp:1761`) and the result type; it
   should also emit `.L<symbol>.end` after the epilogue so the range is exact. This is
   what lets `DebugSession::locals` name a static function and show its locals at all
   (`session.cpp:654-668`). Keep `G` records for data symbols only (already the case,
   `kr232_assembler.cpp:571-585`).
5. **Do not promote recorded allocas on kr232.** Until Track B's producer and the
   krdbg consumer land, pass `promote_recorded = false` for kr232 (and for any target
   whose backend cannot emit `P`), e.g. at `toolchain.cpp:685-686` gate on a target
   capability instead of only `options.optimize >= 2`. This is the ADR 0052 rule —
   "the compiler must not reuse a slot or promote a named variable until a reader
   honours them" (`0052-...md:17-20`) — and it is already violated for kr232 at `-O2`
   today. This is a behaviour change at `-O2` (fewer frame bytes lost), so record the
   measurement with the change; `-O0`/`-O1` output does not change.

Track A needs no KDBG change and no krdbg change: `W`/`K` are already translated
(`src/kdbg/kdbg.cpp:150-187`) and goldens already cover kr232 variables
(`CMakeLists.txt:451-460`, `krdbg_kr232_variables_golden`).

### 3.3 Track B — producer records and ranges

When a kr232 codegen change does keep a named local in a register (machine IR +
allocation, ADR 0051 route), the backend emits, **in addition to the `V` record**:

- bridge directive `.debug_location <function> <name> <R<n>|F<n>> <start_label>
  <end_label>`, which the assembler forwards as `P` (spelling fixed by
  `krcompiler/docs/symbols.md:97-107`; the linker already resolves it to
  `KdbgLocation`, `linker.cpp:884-903`);
- half-open ranges, one record per live interval, several records per variable
  allowed;
- **whole-function** range (`<function>` label to `.L<symbol>.end`) when the value is
  register-resident over the entire body; this is the common case for a leaf promotion
  and is what covers the stretch before the first assignment (KR32's accepted
  contract: reads the register's stale contents, like a fresh frame slot,
  `kr32/docs/debug.md:44-49`);
- **span** ranges only when labels already exist to bracket the interval; never insert
  labels in the pipeline — anchor on the `.Ldbg`/block/function labels the backend
  already emits, and append the `P` bridge text after all passes (`kr32` learned this
  at `implementation.md:4264-4272`, `:4283-4304`);
- only for word-sized scalars and pointers that fit one register (a register pair or an
  aggregate is not representable by one `Location`);
- the register must be stable over the whole interval: not scratch, not clobbered by a
  call inside the range unless saved/restored, not reused for another value;
- the same function name string in `F`, `K`, `W`, `V` and `P` — the C++ label/name
  mismatch is exactly the bug that made KR32's C++ `P` records wrong
  (`kr32/implementation.md:4711-4726`).

If the home slot is no longer maintained after promotion, set VarInfo flag bit 2
(`0x4`; bits 0 address-taken, 1 read-only, others reserved 0 —
`krcompiler/docs/symbols.md:101`) to "home slot unusable". That lets a consumer tell
"outside the range → optimized out" apart from "outside the range → read the slot".
With a whole-function range the flag is redundant, but a future producer that promotes
part of a function needs it, and adding it now is additive (krdbg maps it to a new
`VariableFlag` bit).

No linker change is needed. The kr232 assembler needs only the new directive, the same
kind of forwarding it already does for `.debug_var`/`.debug_type`
(`kr232_assembler.cpp:146-171`).

For KR32 itself the same producer is the one ADR 0052 and `docs/optimize.md` §10 option
C describe (`optimize.md:562-570`); Track B therefore lands once for all targets with a
register-resident codegen.

### 3.4 Track B — krdbg changes

**Status:** implemented (2026-11-27), including the translation, PC-aware selection,
frame handling, `members()` through the resolved location, the Variables display,
and the synthetic/hand-built-payload tests. The producer remains the open half.

1. **Model.** Add a live-range record to the neutral model, matching the PLAN's v1.1
   `Location` record shape (`PLAN.md:1185-1194`):

   ```cpp
   struct LocationInfo {
       std::string function;
       std::string name;
       Location location;        // Register (group/index), FrameOffset or Absolute
       std::uint64_t start = 0;  // half-open, image address space
       std::uint64_t end = 0;
   };
   ```

   `DebugInfo` gains `std::vector<LocationInfo> locations;`. `VariableInfo.location`
   stays the declared/home location from `V`. Add `VariableSlotUnusable = 1U << 4` to
   the `VariableFlag` enum for the `W` flag bit 2 above. This is a change to
   `include/krdbg/debug_info.h`, which PLAN.md freezes with the other interfaces; per
   `AGENTS.local.md` the PLAN gets the interface note in the same change.
2. **Translation.** In `src/kdbg/kdbg.cpp`, after the variables loop, translate every
   `KdbgLocation`: `fp` selects group `fpr`, otherwise `gpr`; index is `reg`; copy
   `function`, `name`, `start`, `end`; sort by `(function, name, start)` for
   determinism. A v1 payload produces none (`KdbgImage.locations` is empty), so v1
   behaviour is bit-identical. `merge_debug_info` must carry the new vector and
   renumber function indexes the way it already renumbers functions/lines
   (`src/core/debug_info.cpp:33-150`).
3. **Selection.** Add `const LocationInfo* location_at(const DebugInfo&, function,
   name, pc)` returning the first range with `start <= pc < end` (KR32's rule,
   `kr32/src/debug/debug.cpp:717-727`). `make_view` becomes PC-aware:
   `make_view(variable, frame_base, pc, frame_index)`. Inside a register range the
   location is that register; otherwise it is `variable.location` unless the variable
   carries `VariableSlotUnusable`, in which case the value is `<optimized out>`.
4. **Frames.** `locals(frame)` already has `frames[frame].pc` and `frame_base`
   (`src/core/session.cpp:626-643`, `:645-669`); pass both into `make_view`. For
   `frame > 0`, a register-located variable cannot be read: `RawFrame` carries only
   `pc` and `frame_base` (`include/krdbg/target.h:97-100`), and the registers hold the
   *current* frame's live values, not the caller's at call time. Report the value as
   unavailable (`?`/`register not available in caller frame`) rather than reading the
   current register and lying. Recovering it properly needs a register snapshot in
   `RawFrame` or a `read_frame_register` on `IDebugTarget`; that is a separate
   interface change and is not needed for the maintainer's requirement (frame 0 is the
   frame being inspected).
5. **Aggregates.** `members(VariableView)` currently re-finds the variable by name+type
   and re-reads through the *declared* location and `frame_base_for(function)`
   (`session.cpp:697-713`). It must use the resolved location carried by the view
   instead, or an expanded register-held aggregate reads the stale slot — the same
   defect KR32 fixed by expanding through the register
   (`kr32/docs/ui.md:233-235`). Register-held aggregates work today through
   `read_variable_storage` for sizes up to 8 bytes
   (`src/core/variables.cpp:52-77`); anything larger cannot be one `P` record.
6. **Surface.** Add `register: R9` / `register: F2` to a row whose resolved location is
   a register (KR32's display, `kr32/docs/ui.md:233-235`) and `<optimized out>` for an
   unavailable variable; keep the UI free of VM types by doing the `gpr`/`fpr` mapping
   in the translator, not in `krdbg/ui` (`AGENTS.local.md` architecture boundaries).
   `VariableView` gains the display facts; `surface.cpp` currently ignores
   `VariableView.location` entirely (`src/ui/surface.cpp:1016-1056`). Update the
   kr232/cr32 variables goldens (`goldens/kr232-variables.hash`,
   `CMakeLists.txt:451-460`).
7. **Compatibility.** `read_payload`/`read_section` signatures do not change; the
   vendored and installed `krc::debug` both decode `Location` (vendored
   `third_party/krc-debug/src/kdbg.cpp:628-651`), so no dependency change is needed.

### 3.5 Precedence and fallback rules to implement

| At the current PC | Result |
|---|---|
| Inside a register range, frame 0 | Read the register (GPR/FPR), slice to `VariableInfo.size` |
| Inside a register range, frame > 0 | Unavailable: `?` ("register not available in caller frame") |
| Outside every range, `VariableSlotUnusable` clear | Read the declared home location (`FrameOffset`/`Absolute`) — the `-O1` home-slot promise |
| Outside every range, `VariableSlotUnusable` set | `<optimized out>` |
| No `V` record for the name | A `P` record alone cannot be typed or sized; the producer must emit `V` too; krdbg ignores orphan locations |
| Two ranges overlap | Producer error: refuse or merge; krdbg takes the first in sorted order to stay deterministic |
| v1 payload, no locations | Exactly today's behaviour |

### 3.6 Change list by file

| Repository | File | Change |
|---|---|---|
| krcompiler | `src/backend/kr232_backend.cpp` | `.addr` filter; `W`/`K` emission; protected-slot set passed to peepholes; `.debug_function`/end label; later `.debug_location` (Track B) |
| krcompiler | `src/asm/kr232_assembler.cpp` | forward `.debug_varinfo`, `.debug_scope`, `.debug_function`, `.debug_location` as `W`/`K`/`F`/`P`; F end/frame/type from directives |
| krcompiler | `src/backend/kr232_cfg.cpp` / `.h` | recorded-slot set in `eliminate_local_dead_frame_stores` and `eliminate_dead_frame_stores` |
| krcompiler | `src/toolchain/toolchain.cpp` | gate `promote_recorded` on target capability (kr232 false until Track B) |
| krcompiler | `docs/symbols.md` | record the kr232 producer rule and the new `W` flag bit |
| krdbg | `include/krdbg/debug_info.h` | `LocationInfo`, `DebugInfo::locations`, unavailable/location fields on views, `VariableSlotUnusable` |
| krdbg | `src/kdbg/kdbg.cpp` | translate `image.locations`; map `fp` to group |
| krdbg | `src/core/session.cpp` | `location_at`, PC-aware `make_view`, frame handling, members use resolved location |
| krdbg | `src/core/debug_info.cpp` | merge locations |
| krdbg | `src/ui/surface.cpp` + `goldens/` | `register: R9`, `<optimized out>`, golden update |
| krdbg | `PLAN.md` | interface note for the `debug_info.h` addition (frozen interfaces rule) |

---

## 4. Tests and fixtures

The suite to add is organised so the kr232 gap is first pinned by a failing test, then
fixed, and so the location consumer is proven without a producer.

### 4.1 krcompiler

1. **`krc_debug_kr232_source_locals`** (new, next to the existing `krc_debug_*` tests).
   Compile `tests/fixtures/c/kr232_promoted.c` (a local `x` assigned twice, a named
   `static` helper, a C++ temporary in a sibling fixture) at `-O0` and `-O1`, link,
   then assert on `--dump-symbols --json`:
   - `x` is present with its frame offset and type;
   - `variableInfo` has `x` as `local` and parameters as `param`;
   - no name starts `temporary.`/`call.`/`operator.`/`subscript.`;
   - the `static` helper has a `functions` entry and its locals have records;
   - `locations` is `[]` until Track B.
2. **`krc_debug_kr232_local_store_survives_o1`**. Assemble the same source with `-S`
   and assert a store to the slot named by `.debug_var` exists at `-O1` (a shape
   assertion, not golden bytes), or link and execute a fixture that reads `x` after a
   breakpoint-level statement. This is the red test for §2.3.
3. **`krc_debug_kr232_location_bridge`**. Hand-built `.debug_info` with
   `S/L/F/V/P/K/W/T` for `target_id 5`, run through `attach_kdbg` as the existing v1.1
   tests do (`krcompiler/docs/kdbg-v1-1-log.md:137-138`), then assert `locations=1`,
   the register/fp decode, the resolved half-open range, `varinfo` kind and scope, and
   both dumper forms. This is the producer-independent path test and can land before
   the kr232 producer.
4. **`krc_debug_kr232_o2_recorded_promotion`**. With Track A's gate, assert `-O2` still
   lists named locals (or, once Track B lands, lists them with a whole-function
   `locations` entry); the test encodes the policy so it cannot silently regress to
   "variables are gone".
5. **Determinism and level identity** for the new directives, as the existing
   `krc_optimization_levels`/determinism tests do.

### 4.2 krdbg

1. **`kdbg_translates_location_records`** in `tests/test_kdbg.cpp`: construct a
   `krc::debug::KdbgImage` with `locations` (one `gpr`, one `fpr`, two ranges for one
   variable), `encode_kdbg`, `read_payload`, assert `DebugInfo.locations` has the
   right function/name/group/index/half-open addresses. Also assert a payload without
   locations leaves the vector empty.
2. **`kdbg_location_records_are_not_required_for_v1`**: a v1 payload still translates
   byte-identically (the existing `fake.kdbg` parity test already gates this;
   `tests/test_kdbg.cpp:110-150`, `:218-245`).
3. **`session_reads_a_register_local_inside_its_range`** in `tests/test_session.cpp`
   (or a new `test_locations.cpp`): synthetic `DebugInfo` with a variable whose home is
   `FrameOffset` and a register range; `FakeTarget` holds a frame slot value and a
   different register value; assert `locals(0)` returns the register value at a PC
   inside the range and the frame value outside it. This is the consumer cut that
   needs no producer.
4. **`session_reports_an_optimized_out_local`**: `VariableSlotUnusable` and a PC
   outside every range → value `<optimized out>`, not a slot read.
5. **`session_register_local_in_a_caller_frame_is_unavailable`**: same variable in
   frame 1 → `?`, not the current register.
6. **`session_members_of_a_register_local_read_the_register`**: extend the existing
   aggregate/pointer expansion tests (`tests/test_session.cpp:239-276`) to a
   register-located aggregate, proving `members()` uses the resolved location.
7. **`kr232_scenario_promoted_local_listed`** in `tests/test_kr232_scenarios.cpp`
   (mirroring `kr232_scenario_inspection_frames_and_locals`,
   `tests/test_kr232_scenarios.cpp:130-155`): with the new committed
   `promoted.kr232`, run to a line inside the function, find the local in
   `session.locals(0)`, and assert both its presence and its value. This is the
   maintainer's acceptance test at the default level.
8. **`kr232_scenario_no_temporary_variables`** and
   **`kr232_scenario_static_function_locals`** using the same/new fixtures; both should
   fail before Track A.
9. **Golden:** update `goldens/kr232-variables.hash` and the kr32 variables golden if
   the row text gains `register:`/`<optimized out>`; `krdbg_kr232_variables_golden`
   already drives `aggregate.kr232` (`CMakeLists.txt:451-460`), and
   `tests/test_golden.cpp:192-196` covers variable expansion hashes.

### 4.3 Fixtures

- `tests/fixtures/kr232/promoted.c` — a function with one scalar local assigned twice
  to distinct values and read after both, one local assigned but never read by the
  program (§2.3 regression), one named `static` helper with a local, and one C++
  object temporary in a sibling `.cpp` for the temporary filter.
- `tests/fixtures/kr232/promoted.kr232` — built by
  `tests/fixtures/kr232/regenerate.ps1` at the default level (`-O1`; the script passes
  no `-O` flag, `regenerate.ps1:33`), committed, and covered by
  `CheckFixtureDrift.cmake` so it cannot drift from its source. Add the fixture to the
  table in `tests/fixtures/kr232/README.md`.
- For the range path, until a kr232 producer exists, the hand-built bridge fixture in
  §4.1(3) and the in-test `KdbgImage` in §4.2(1) are the fixtures. Do not hand-edit a
  `.kr232` container to inject a `.kdbg` payload: the payload length changes and the
  container's section table would have to be rebuilt in the test, which would test the
  test helper, not the product.

### 4.4 Commands and gates

- Producer evidence (used in §6):
  `krc -S --target=kr232 -O1 file.c -o file.s`, then `krc -c` + `krld --entry=<fn>` +
  `krc --dump-symbols [--json] image.kr232`.
- krcompiler gate: the repository gate in `krcompiler/docs/kr-build-instructions.md`
  (its presets run the unit suite that holds `krc_debug_*`).
- krdbg: `powershell -File scripts/build-debugger.ps1` then the `windows-msvc-kr232`
  preset's CTest (`CMakePresets.json:35-49`, `:121-126`; tests registered at
  `CMakeLists.txt:337-460`), including `krdbg_kr232_unit`,
  `krdbg_kr232_variables_golden` and the fixture-drift test.
- Add the new kr232 fixtures through `scripts/build-debugger.ps1`/`regenerate.ps1` and
  the family gate (`../kr232/scripts/check-family.ps1 -Only krdbg`), not by a bare
  `cmake` configure (`AGENTS.local.md`).

---

## 5. Risks and limits

- **No honest kr232 producer today.** Track B's `P` record requires a register home;
  kr232's emitter has none (scratch-register materialisation). A `P` record emitted now
  would expire at every instruction boundary. That is why Track A is required and
  Track B is gated on a codegen change.
- **Register values are only live in the current frame.** `RawFrame` has no register
  snapshot (`include/krdbg/target.h:97-100`), so an outer frame's register-located
  local cannot be read. Showing `?` is correct; reading the current register would be
  wrong. A snapshot is a later, separate interface addition.
- **Whole-function ranges read stale registers before the first assignment.** This is
  the accepted KR32 contract (`kr32/docs/debug.md:44-49`), not a defect; narrowing to
  the first store would report "unavailable" instead and makes the range depend on a
  line position. Document whichever the producer chooses.
- **Label churn.** Ranges must anchor on labels already emitted; inserting labels after
  the pipeline caused out-of-range records in KR32
  (`kr32/implementation.md:4264-4272`, `:4283-4304`).
- **A `P` record carries no type/size.** It must accompany a `V` record for the same
  variable; krdbg ignores orphan locations rather than inventing a type.
- **`-O2` is a product change.** Gating `promote_recorded` for kr232 changes `-O2`
  output (frame bytes up, no variables lost). Measure it and record it; `-O0`/`-O1`
  bytes are unchanged.
- **Interface change.** Adding a field to `debug_info.h` and `VariableView` touches the
  PLAN's frozen interface set; `PLAN.md` must record it before the code lands
  (`AGENTS.local.md`, definition of done).
- **Adjacent defects found and not fixed here** (report, do not fold in):
  the stale "NOT WIRED IN" comment at `register_allocator.h:3-10`; the KR32/kr16
  display-name/symbol mismatch that hides C++ member locals in krdbg
  (§2.5); the missing definition-line record for kr232 documented in ADR 0061
  (`krcompiler/docs/symbols.md:73-79`).

---

## 6. Evidence appendix (measured, not inferred)

Installed package: `krc 0.2.0, build Debug, revision 80960f0` (ancestor of the tree's
`HEAD a9e22a0`; `ff72fce` added kr232 type records after it, so `types=0` below is an
artifact of the installed revision — the `V`/promotion/promotion-refusal paths are
unchanged by the later commits).

1. **The `-O1` home-slot path (KR32 and kr232).** Source
   `int compute(int a,int b){int x=a+b;x=x*2;return x;}` compiled with
   `krc -c --target=... -O1` + `krld --entry=compute`:
   - kr32: `KDBG target=1 ... variables=3 ... locations=0 scopes=1 varinfo=3`;
     `variable compute.a offset=8`, `compute.b offset=12`, `compute.x offset=32`.
   - kr232: `KDBG target=5 ... variables=3 ... locations=0 scopes=0 varinfo=0`;
     `variable compute.a offset=4294967276` (= -20), `compute.b offset=-24`,
     `compute.x offset=-28`. The `-O1` assembly contains `.debug_var compute x -28 0 4`
     and no `P`.
2. **`-O2` drops named locals, with no fallback.**
   - kr32 `-O2`: `variables=2` (`a`, `b`; `x` gone), `locations=0`, frame 40→24.
   - kr232 `-O2`: `variables=0`, `locations=0` — parameters gone too.
3. **kr232 deletes the last store to a recorded slot.** Source
   `int fixed(int a){int unused=a+1;return a;}` at `-O1`:
   - kr232 assembly: `.debug_var fixed unused -16 0 4` present, no
     `st [r13-16]` anywhere.
   - kr32 assembly: `.Ldbg.fixed.2: STORE R14, [R7 + 20]` for the same local.
4. **kr232 lists compiler temporaries.** C++
   `struct P{int x;P(int v){x=v;}int get()const{return x;}}; int f(){return P(3).get();}`
   at `-O1`:
   - kr232: `variables=5`, including `variable f.temporary.record offset=-8` and
     `variable f.temporary.this offset=-12`.
   - kr32: `variables=3` (the three parameters only).
5. **kr232 has no `F` record for a `static` function.** Source with
   `static int helper(int){...}` and global `entry`:
   - kr232: function table has `entry` only; variables include `variable helper.a` and
     `variable helper.doubled`.
   - kr32: both `helper` and `entry` in the function table.
6. **krdbg did not read locations** (measured before Track B landed; the consumer
   now translates and applies them — see §1.4). `src/kdbg/kdbg.cpp` had no
   `image.locations` reference; `include/krdbg/debug_info.h` had no range
   vector. The decoder did produce them: vendored
   `third_party/krc-debug/src/kdbg.cpp:628-651`, dumpers at `:797`, `:849`, `:906-915`.
   The neutral register path worked without it — `fake_debug_info`'s `argc`
   (`gpr:0`) and `held` (`core:5`) were read in `tests/test_session.cpp`.
7. **The Variables-panel source is `name = value`.** `src/ui/surface.cpp:1030`,
   `:1049`; no `VariableView.location` use anywhere in `src/ui`.
