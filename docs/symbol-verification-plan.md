# Symbol verification plan for kr232

Status: design for the verification work only. No source change is part of this
plan, and nothing in it is claimed to pass yet: every row marked **RED** below
is either measured failing on 2026-11-27 with the tools and fixtures in this
checkout, or blocked on a named prerequisite (§4). The legend in §11 says which.

Scope: prove that KRCompiler's kr232 debug symbols are good enough for a
debugger to be useful — C++ and assembler source lines, local and global
variables, aggregates, and call stacks — and pin each property with a test in
`krdbg_kr232_tests` (plus a small number of producer-side tests in KRCompiler).
Everything is expressed against the observable session API in
`include/krdbg/session.h`: `line_at`, `function_at`, `locals`, `globals`,
`members`, `call_stack`, `info`, `step`, and `add_line_breakpoint`.

Fixtures are check-in images regenerated from source by
`tests/fixtures/kr232/regenerate.ps1` and compared byte for byte by
`tests/fixtures/kr232/CheckFixtureDrift.cmake`. New fixtures follow that rule;
none is hand-made.

## 1. What "excellent symbols" means here

| Id | Requirement | Observable |
|---|---|---|
| R1 | C++ source lines | A line breakpoint on a real C++ source line stops at that line, in that file. |
| R2 | Stepping stays in the source | `LineOver`/`LineIn` stops only on line records that name real lines of the file being shown, and advances through the statement sequence. |
| R3 | Assembly source lines | The same for a hand-written `.s` program (physical line numbers). |
| R4 | Locals and parameters | `locals(0)` lists the source variables of the current function with correct names and values; parameters are recoverable from the caller. |
| R5 | Caller-frame locals | `locals(1)` reads the caller's variables through the caller's frame. |
| R6 | Globals | `globals()` lists external-linkage globals with type, size and value; aggregate globals expand through `members()`. |
| R7 | Internal linkage | File-scope `static` globals and `static` functions are visible (the KR32 behaviour). |
| R8 | Call stacks | `call_stack()` names every frame, each frame's PC lies inside its function's bounds, and each frame resolves to its own source file. |
| R9 | Types and members | Locals, parameters and globals with struct/array types expand to named members with correct offsets and values. |
| R10 | `this` | A member function's frame lists `this`. |
| R11 | Variable kinds | Parameters carry `VariableParam`; statics carry `VariableStatic`/`Global` as appropriate. |
| R12 | No compiler temporaries | Temporary slots (`argument.copy`, `cond.tmp`, `logic.slot`, …) never appear in the Variables view. |
| R13 | Promotion guarantee at `-O1` | A local shaped so a promotion pass would like to keep it in a register is still listed with the right value while it is live, including in a caller frame. |
| R14 | Promotion is declared unsupported at `-O2` | KDBG v1 cannot express a register home. kr232 must not promote recorded allocas until a register producer exists, so `-O2` keeps every named local in its home slot and lists it; the unsupported case is a genuinely register-resident local, which only a future codegen produces (`docs/register-promoted-locals.md` Track A/B). |
| R15 | Register-held locals (future) | A `.kdbg_loc`/`P` register location is usable, carries a live range, and is resolved against the PC of the frame being shown. |

## 2. How the debugger consumes the symbols

The chain is short and every stage is a possible leak point:

1. **Producer.** For C/C++ the kr232 backend emits assembly containing debug
   directives — `.debug_line`, `.debug_var`, `.debug_type`, `.debug_global`
   (`../krcompiler/src/backend/kr232_backend.cpp:1384-1425`,
   `:1494-1512`, `:1764-1788`, `:1975-1986`) — and the kr232 assembler turns
   them into the `.debug_info` bridge: `S`, `L`, `F`, `G`, `V`, `T/C/M/A/R`
   (`../krcompiler/src/asm/kr232_assembler.cpp:510-592`).
2. **Linker.** `krld` merges bridge records and writes KDBG v1.1
   (`../krcompiler/src/linker/linker.cpp:746-1019`). Two filters matter:
   a line record is dropped when it falls outside every function span
   (`:979-985`), and the assembler emits `F`/`G` records only for labels marked
   `.globl` or `.weak` (`../krcompiler/src/asm/kr232_assembler.cpp:561-583`).
3. **krdbg translation.** KDBG → `DebugInfo`: sources, lines, functions,
   symbols, variables, types and v1.1 variable kinds are translated
   (`src/kdbg/kdbg.cpp:62-218`). `image.locations` (`.kdbg_loc`) is *not*
   applied — a known limit listed in `krdbg-newtarget.md:264-279`.
4. **Session.** `locals(frame)` filters `info().variables` by the frame's
   function name and drops globals; `globals()` keeps flagged globals and
   function-less entries; `members()` reads through the variable's location
   (`src/core/session.cpp:645-685`, `:697-777`); frame base arithmetic is in
   `src/core/variables.cpp:44-91`.
5. **Line stepping.** `LineIn` stops on the first valid line of a new line key
   or an entered callee (`src/core/session.cpp:458-483`); `LineOver` stops on a
   new line in the same function and skips the callee once the frame chain
   grows (`:484-519`). Both depend on real line records, not just on `F`
   records.

## 3. Current measured state

Measured 2026-11-27 against this checkout:
`../krcompiler` is at `a9e22a0` **plus uncommitted changes** in
`src/backend/kr232_backend.cpp` and `src/asm/kr232_assembler.cpp` that add the
`.debug_line` directive (`git -C ../krcompiler status --porcelain`).
A compiler built from the committed tree is referred to below as **old krc**;
`build/krc-new-prefix/bin/krc.exe` (built 11:17 from the working tree) as **new
krc**. Symbols were measured with `krc --dump-symbols --json`; line records are
not in that JSON, so line evidence comes from decoding the image's `.kdbg_line`
section and comparing each record's line with `wc -l` of its source.

| Evidence | Measured result | Consequence |
|---|---|---|
| Committed `tests/fixtures/kr232/nested_frames.kr232` | source `kr232_nested_frames.c` has **10** lines; its 16 line records span **7…29**, all generated-assembly numbers | the checked-in C line tables are not source line tables |
| Committed `tests/fixtures/kr232/aggregate.kr232` | `aggregate.c` has **25** lines; its 107 line records span **27…141** | same |
| `build/startrekng-kr232/startrekng_script.kr232` (built 11:22 with old krc) | `main_script.cpp` has **8** lines, records span **121…240**; `console.cpp` **137** lines, records **59…1861**; `libc_full.c` **2932** lines, records up to **34308** | the existing startrekng scenario passes because it never checks that a recorded line exists in its file |
| A 10-line C probe rebuilt with new krc | body instructions carry lines **2/3/4** and **7/8/9**; the function prologue carries generated-assembly lines **7/11/12** | `.debug_line` fixes statements but is emitted *after* the prologue, so the function-entry records are still generated numbers |
| A C++ probe rebuilt with new krc | body lines **12/13/14, 19/20, 24/25** correct; prologue records **29/33/34** | same gap |
| `krc --dump-symbols --json` on a C++ image with `static int hidden(...)` and `static int file_total` | `functions` has no `hidden`; sized `symbols` have no `file_total`; only `external_total` and `g_vec` | internal-linkage functions and data are invisible |
| same JSON | `variableInfo: []` for every kr232 image | no parameter/static kinds; everything is `VariableLocal` after translation (`src/kdbg/kdbg.cpp:169-186`) |
| same JSON on `aggregate.kr232`/`kr232_frame_objects.c` | a compiler temporary `argument.copy`/`cond.tmp` is listed as a variable | the kr232 backend records every named alloca, not only `<name>.addr` (contrast `../krcompiler/src/backend/kr32_backend.cpp:1559-1584`) |
| C++ probe at `-O2` | only aggregate locals remain (`helper/local`, `main/counter`); scalar named locals disappear from `variables` | promotion of `<name>.addr` removes the alloca and therefore the only debug record |

Four consequences follow, and they are the plan's prerequisites:

- **G1 — line tables.** C++/C line verification is blocked until the
  `.debug_line` change is committed, the installed prefix rebuilt, and every
  kr232 fixture (existing and new) regenerated. The prologue/definition-line
  record is a further fix: the directive must be emitted before the first
  prologue instruction so a function's entry address carries its definition
  line, not a generated-assembly line.
- **G2 — internal linkage.** Static functions get no `F` record and static
  globals no `G` record; worse, a static function's line records are then
  dropped by the linker because they fall in no function span. This breaks
  R7, R8 for static frames, and any breakpoint inside a static function.
- **G3 — promotion fidelity.** At the default level (`-O1` since ADR 0060) named
  variables are deliberately kept in frame slots
  (`../krcompiler/src/ir/passes.cpp:326-339`; the driver only passes
  `promote_recorded = true` at `options.optimize >= 2`,
  `../krcompiler/src/toolchain/toolchain.cpp:661-687`), so the home-slot
  promise is verifiable today. At `-O2` named locals may be promoted and KDBG
  v1 cannot express a register location (`../krcompiler/docs/optimize.md` F2);
  no kr232 producer emits `P`/`.kdbg_loc`, and krdbg neither translates
  locations nor models a live range. The committed design
  `docs/register-promoted-locals.md` settles this in two tracks: Track A makes
  the home-slot promise true and stops kr232 promoting recorded allocas at any
  level; Track B adds the location consumer (krdbg first) and a producer only
  once kr232 can keep a value in a register.
- **G4 — recorded-slot stores.** The kr232 peepholes can delete a store to a
  slot a `V` record names, so even a home-slot value can be stale before any
  promotion exists. `docs/register-promoted-locals.md` §2.3 measured this and
  Track A protects those slots; until then a "correct value" assertion on a
  named local is only as good as the store it reads.

## 4. Prerequisites and landing order

| Phase | Work | Why |
|---|---|---|
| P0 | Commit the KRCompiler `.debug_line` fix, including a definition-line record before the prologue; rebuild the installed krc/krld prefix on Windows and in the WSL mirror. | G1 |
| P1 | Regenerate **all** kr232 fixtures with that compiler (six existing + five new); `CheckFixtureDrift` matches. | G1; the existing images are not trustworthy line-wise |
| P2 | KRCompiler Track A (`docs/register-promoted-locals.md` §3.2): internal-linkage `F`/`G` records (mirror `../krcompiler/src/backend/kr32_backend.cpp:1072-1080`), parameter/local `W`/`K` records, the `.addr` filter for variables, `.L<symbol>.end` and `F` for every function, protected recorded slots in the peepholes, and `promote_recorded = false` for kr232 at every level. | G2, G4, R11, R12, R14 |
| P3 | Landing of the green rows of §7–§11 as CTest rows; each phase lands only tests that pass. | keeps the gate honest |
| P4 | KRCompiler Track B + krdbg location consumer (`docs/register-promoted-locals.md` §3.3–§3.5): `LocationInfo` in the model, `location_at`, PC-aware `make_view`, frame>0 unavailable rule, `VariableSlotUnusable`; the kr232 `P` producer only once kr232 codegen keeps a value in a register. | G3, R15 |

Tests land in this order. A test whose prerequisite is not in is **not
committed**; the matrix in §11 records it as pending, so a green gate never
implies a verification that is not running.

## 5. Fixtures to add

All sources live under a new `tests/fixtures/kr232/src/` directory in the
krdbg tree (KRCompiler is read-only for this work), and all are compiled from
the krdbg repository root with forward-slash relative source names, like
`aggregate.c` is today:

```text
tests/fixtures/kr232/src/symbols_cpp.cpp          -> symbols_cpp.kr232
tests/fixtures/kr232/src/symbols_pair_main.cpp    -> symbols_pair.kr232
tests/fixtures/kr232/src/symbols_pair_math.cpp
tests/fixtures/kr232/src/symbols_pair_shared.hpp
tests/fixtures/kr232/src/symbols_asm.s            -> symbols_asm.kr232
tests/fixtures/kr232/src/include_heavy.cpp        -> symbols_include.kr232
tests/fixtures/kr232/src/include/{vec,player,world,script}.hpp
tests/fixtures/kr232/src/promoted.cpp             -> promoted_o1.kr232   (-O1)
```

Five new containers (F1–F4 and F6) with their sources; F5 reuses the existing
startrekng build. The six existing containers are regenerated, not replaced.

### F1 — `symbols_cpp.cpp` (single file; parameters, locals, aggregates, `this`, both linkages)

```cpp
struct Vec { int x; int y; };
struct Samples { int values[3]; };

int external_total = 5;                 // external linkage
static int file_only = 100;             // internal linkage (R7, RED before P2)
Vec g_vec = {3, 4};                     // aggregate global (R6/R9)
Samples g_samples = {{1, 2, 3}};        // nested aggregate global
int g_values[4] = {10, 20, 30, 40};     // array global

class Counter {
public:
    int bump(int delta);
    int stored;
};

static int hidden_add(int n) {          // static function (R7/R8, RED before P2)
    int adjusted = n + file_only;
    return adjusted;
}

int Counter::bump(int delta) {          // this, member, params, locals (R4/R10)
    int before = stored;
    int twice = delta + delta;
    stored = before + twice + hidden_add(before);
    return stored;
}

int sum_vec(Vec v) {                    // aggregate parameter (R4/R9)
    int total = v.x + v.y;
    return total;
}

int loop_sum(int count);

int main(void) {
    Counter counter;
    counter.stored = 7;
    Vec local = {1, 2};
    int pair[2];
    pair[0] = sum_vec(local);
    pair[1] = loop_sum(2);
    int bumped = counter.bump(3);
    return bumped + pair[0] + pair[1] + external_total + g_values[0];
}

int loop_sum(int count) {               // locals shaped like promotion candidates (R13)
    int total = 0;
    int index = 0;
    while (index < count) {
        total += g_values[index];
        index = index + 1;
    }
    return total;
}
```

Compile at `-O0` explicitly (the `krc` default is `-O1` since ADR 0060; line
stepping should not depend on optimiser decisions), link with `crt0.o` (no
libc). Expected symbol shape after P1: functions `_start`,
`Counter_bump`, `sum_vec`, `loop_sum`, `main` (and `hidden_add` after P2);
variables `Counter_bump/{this, delta, before, twice}`,
`sum_vec/{v, total}`, `main/{counter, local, pair, bumped}`, plus the
`argument.copy` temporary in `main` until P2; sized globals
`external_total(i32,4)`, `g_vec(Vec,8)`, `g_samples(Samples,12)`,
`g_values([4_x_i32],16)` (and `file_only` after P2); types `Vec`, `Samples`,
`[2_x_i32]`, `[3_x_i32]`, `[4_x_i32]`, `Counter`, `i32`.

### F2 — `symbols_pair_*` (two units, cross-unit calls, a static function)

`symbols_pair_shared.hpp`:

```cpp
struct Vec { int x; int y; };
extern int pair_global;
int cross_add(Vec v, int n);
int cross_total(int seed);
```

`symbols_pair_math.cpp`:

```cpp
#include "symbols_pair_shared.hpp"

int pair_global = 40;
static int pair_hidden = 2;             // internal linkage (R7, RED before P2)

static int internal_scale(int n) {      // static function (R7/R8, RED before P2)
    return n * pair_hidden;
}

int cross_add(Vec v, int n) {
    int sum = v.x + v.y + n;
    return internal_scale(sum);
}

int cross_total(int seed) {
    return seed + pair_global;
}
```

`symbols_pair_main.cpp`:

```cpp
#include "symbols_pair_shared.hpp"

int main(void) {
    Vec v = {1, 2};
    int base = 4;
    int a = cross_add(v, 3);
    int b = cross_total(a + base);
    return a + b;
}
```

Compile both units at `-O0` with `--far-calls` and
`-I tests/fixtures/kr232/src` (krc does not search the including file's
directory for a quoted include), link with `crt0.o`. Expected: two `SourceFile`
entries plus crt0; functions `cross_add`,
`cross_total`, `main` (and `internal_scale` after P2); `locals(0)` in
`cross_add` is `{v, n, sum}` and `locals(1)` is `main`'s `{v, base, a, b}`;
`pair_global` is a sized global (and `pair_hidden` after P2).

### F3 — `symbols_asm.s` (assembly lines, `.globl` function, named data object)

```asm
; Assembly symbol fixture. The function prologue matches the ABI's ENTER
; chain so the debugger can unwind the caller.
.globl asm_add
.globl asm_counter
.globl asm_table
.text
asm_add:
  enter #8, #0
  .debug_var asm_add asm_value -4 i32 4
  st [r13-4], r2
  ld r2, [r13-4]
  add r2, r3
  leave #0
  ret
.data
asm_counter:
  .word 7
asm_table:
  .word 11
  .word 22
  .word 33
.text
.globl _start
_start:
  enter #0, #0
  ld r2, #2
  ld r3, #3
  call asm_add
  ld r7, #0
  ld r2, #0
  sys
```

Link alone (`krld --entry=_start`, base 0; no crt0 because the fixture defines
`_start`). The `.debug_var` line is the documented assembler bridge directive
(`../krcompiler/src/asm/kr232_assembler.cpp:146-152`); it is what lets this
fixture cover an asm frame-relative local as well. Expected: functions
`asm_add` and `_start` with correct spans; line records on the physical lines
of the instructions (hand-written assembly has no `.debug_line` directives, so
the assembler's own line numbers are the source lines); sized data globals
`asm_counter(4)` and `asm_table(12)`, bounded by the next label/end of `.data`.
`asm_value`'s `type_name` is empty unless the fixture also emits
`.debug_type T i32 4`; the local assertion therefore checks name, size, value
and its frame location, not the type name.

### F4 — `include_heavy.cpp` (startrekng-shaped include set)

```cpp
// include_heavy.cpp
#include "vec.hpp"
#include "player.hpp"
#include "world.hpp"
#include "script.hpp"

int Player::shield() { return power + 1; }
int World::tick(int delta) {
    player.power = delta;
    return player.shield();
}

int g_script_length = 3;

int main(void) {
    World world;
    world.player.power = 4;
    int result = world.tick(world.player.power);
    return result + g_script_length;
}
```

with `include/vec.hpp` (`struct Vec { int x; int y; }`), `include/player.hpp`
(`class Player { public: int shield(); int power; }`), `include/world.hpp`
(`#include "player.hpp"` then
`class World { public: int tick(int); Player player; }`), `include/script.hpp`
(`extern int g_script_length;`; the definition is in the `.cpp`).
Compile at `-O0` with `-I tests/fixtures/kr232/src/include`, link with `crt0.o`.
Expected: only `include_heavy.cpp` is a source (headers never become `S`
records); types `Vec`, `Player`, `World` carry their members with the offsets
the headers define; the line table stays relative to `include_heavy.cpp`.

### F5 — startrekng (no new source)

`build/startrekng-kr232/startrekng_script.kr232` is already built by
`scripts/build-startrekng-kr232.ps1` and already tested by
`kr232_scenario_startrekng`. The new verification adds assertions that its
line table is truthful for all nine units plus `libc_full.c`/`fileio_full.s`,
and that frames resolve to the right source file. The image stays transient;
the row is registered only when the image exists, exactly as
`krdbg_kr232_startrekng_golden` is today (`CMakeLists.txt:409-428`).

### F6 — `promoted.cpp` (`-O1` promotion guarantee; `-O2` unsupported)

```cpp
int eligible(int n) {
    int doubled = 0;            // frame slot at -O1; promoted-and-gone at -O2
    doubled = n + n;            // token A
    doubled = doubled + 1;      // token B
    return doubled;             // token C
}

int caller(int seed) {
    int scaled = seed * 2;      // caller-frame slot used by the frame-1 assertion
    return eligible(scaled);
}

int address_taken(int n) {
    volatile int sink = n;      // never promoted: its address is observable
    return sink;
}

int main(void) {
    int a = caller(5);
    int b = address_taken(3);
    return a + b;
}
```

Compile at the default `-O1` (no `-O` flag; ADR 0060) to `promoted_o1.kr232`,
link with `crt0.o`. `docs/register-promoted-locals.md` §4.3 proposes a C
`promoted.c`/`promoted.kr232` for the same purpose; if that fixture lands
first, adopt it and keep only the caller-frame and stepping assertions from
this section instead of adding a second promoted fixture. A `-O2` image is
**not** committed: it tests a policy (Track A stops kr232 promoting recorded
allocas) rather than a per-level data structure, and that policy belongs to
the KRCompiler producer test in §8.3. The limitation is recorded in §7.6.1.

## 6. Test helpers and assertion vocabulary

New file `tests/test_kr232_symbols.cpp`, added to `krdbg_kr232_tests`
(see §8). It uses only the public session API and the fixture directory
`KRDBG_SOURCE_ROOT "/tests/fixtures/kr232"`, like `test_kr232_adapter.cpp`
does. Suggested local helpers:

- `bool load_fixture(const char* name, LoadedProgram&, std::string& error)` —
  wraps `krdbg::kr232adapter::load_image`, the same as
  `test_kr232_scenarios.cpp:29-40`.
- `std::string resolve_recorded(std::string_view recorded)` — try
  `KRDBG_SOURCE_ROOT/<recorded>`, then
  `KRDBG_SOURCE_ROOT/../krcompiler/<recorded>`; used to read the fixture
  source text and assert stored lines exist.
- `int line_of_token(file, token)` — returns the 1-based line of the unique
  line containing `token`; **fails** if the token is absent or not unique.
  Assertions reference tokens, not constants, so editing a fixture cannot
  silently re-point a check.
- `const LineInfo* line_record(session, file, int line)` and
  `bool stop_on_line(session, file, int line)` — adds the line breakpoint,
  runs with a bounded budget, checks `Retired`, `pc() == record->address`,
  `line_at(pc)->line == line`, and that `line_at(pc)` resolves to the expected
  file. The record's presence is itself the "the producer emitted a record for
  this source line" assertion.
- `std::string line_file(session, pc)` and an invariant helper
  `check_line_records_are_in_source(session, file)`: every `LineInfo` whose
  source is `file` has `1 <= line <= source_line_count(file)`. This is the
  single check that catches a generated-assembly line number leaking into a
  source file.
- `const VariableView* local(session, frame, name)` /
  `global(session, name)` — linear scan; `CHECK(found)` at the call site with
  the name in the failure message is not available in this framework, so scan
  and `CHECK(found != nullptr)`.
- `std::string frame_source(session, const FrameView&)` — `line_at(frame.pc)`
  → `info().sources[...]` path; a frame without a line record fails the check.
- `std::vector<int> line_sequence(session, StepMode mode, int expected_stops)`
  — steps and records `line_at(pc)->line`; any stop with no line record or a
  line outside the file fails.

Values are compared with `CHECK_EQ` on the formatted strings the session
produces (`"7"`, `"{...}"`, `"[2]"`) plus `size`, `type_name`,
`location.kind`, and (for kinds) `session.info().variables` flags, because
`VariableView` does not carry flags.

## 7. Exact assertions per fixture

Through all of §7: a stop is valid only when `line_at(pc)` is non-null, its
`SourceFile` is the expected fixture, and the line is within that file's line
count (the invariant helper above). "Token line" means `line_of_token`.

### 7.1 F1 `symbols_cpp.kr232`

1. `kr232_symbols_cpp_breakpoint_line_equality` — `stop_on_line` on the token
   `stored = before + twice` succeeds; `info().sources` names
   `symbols_cpp.cpp`; the breakpoint's resolved address equals the address of
   the `LineInfo` for that (source,line).
2. `kr232_symbols_cpp_line_stepping_stays_in_source` — from the token line
   `int before = stored;`, `step(LineOver)` must stop next on the token line
   `int twice = delta + delta;`, then on `stored = ...`, then on
   `return stored;`; every stop's file is `symbols_cpp.cpp` and every stop's
   line is one of those token lines. (Stepping into the static `hidden_add`
   belongs to `kr232_symbols_cpp_internal_linkage`, because that is RED until
   P2.)
3. `kr232_symbols_cpp_locals_frame0` — at the `stored = ...` line:
   `locals(0)` names are exactly `{this, delta, before, twice}`; `delta` is
   `"3"`, `before` is `"7"`, `twice` is `"6"`; `this` has
   `type_name == "ptr<Counter>"`; `location.kind == FrameOffset` for every
   variable (no register locations at `-O0`).
4. `kr232_symbols_cpp_aggregate_param` — at the `return total;` line of
   `sum_vec`: `locals(0)` contains `v` with `type_name == "Vec"`, `size == 8`,
   `members(v)` is `{x = "1", y = "2"}` (the argument is `local = {1,2}`).
   `total` is `"3"`.
5. `kr232_symbols_cpp_globals_and_members` — before any run, `globals()`
   contains `external_total` (`type_name == "i32"`, `size == 4`, value `"5"`),
   `g_vec` (`Vec`, 8, members x=`"3"`, y=`"4"`), `g_values`
   (`[4_x_i32]`, 16, `value == "[4]"`, members `[0]..[3]` = 10, 20, 30, 40),
   and `g_samples` (`Samples`, 12, member `values` expands to three ints
   1, 2, 3).
6. `kr232_symbols_cpp_internal_linkage` — RED until P2: `globals()` contains
   `file_only` (`i32`, 4, `"100"`); steps into `hidden_add` stop on its token
   lines and `call_stack()[0].function` is `hidden_add`.
7. `kr232_symbols_cpp_parameters_are_params` — RED until P2: in
   `session.info().variables`, the entries `(Counter_bump, delta)`,
   `(Counter_bump, this)`, `(sum_vec, v)`, `(loop_sum, count)` have
   `flags & VariableParam`; `(Counter_bump, before)` has
   `flags & VariableLocal` and not `VariableParam`.
8. `kr232_symbols_cpp_no_compiler_temporaries` — RED until P2: at `main`'s
   call to `sum_vec`, `locals(0)` names are exactly
   `{counter, local, pair, bumped}` — no `argument.copy`, `cond.tmp` or
   `logic.slot`.

### 7.2 F2 `symbols_pair.kr232`

1. `kr232_symbols_pair_cross_unit_breakpoint` — `stop_on_line` on the token
   `int sum = v.x + v.y + n;` in `symbols_pair_math.cpp` fires while the
   caller is in `symbols_pair_main.cpp`; stopped file is `symbols_pair_math.cpp`.
2. `kr232_symbols_pair_frames_and_sources` — at that stop,
   `call_stack().size() == 2`; `[0].function == "cross_add"`,
   `[1].function == "main"`; `[0].pc == session.pc()`; each frame's pc lies in
   `[function_at(pc)->begin, function_at(pc)->end)`; `frame_source(0)` ends in
   `symbols_pair_math.cpp` and `frame_source(1)` ends in
   `symbols_pair_main.cpp`; the two `frame_base` values differ.
3. `kr232_symbols_pair_frame1_locals` — at that stop: `locals(0)` has `v`
   (`Vec`, members x=`"1"`, y=`"2"`), `n` = `"3"`, `sum`; `locals(1)` has
   `v` (members x=`"1"`, y=`"2"`) and `base` = `"4"`; `a`/`b` are not
   asserted because the call has not returned.
4. `kr232_symbols_pair_line_stepping_into_other_unit` — from the token
   `int a = cross_add(v, 3);` `step(LineIn)` stops in
   `symbols_pair_math.cpp`; from the same line `step(LineOver)` stops on the
   next token line back in `symbols_pair_main.cpp` (`int b = ...`) and
   `line_file(pc)` is `symbols_pair_main.cpp` at every stop.
5. `kr232_symbols_pair_static_function` — RED until P2: stop on the token
   `return n * pair_hidden;` inside `internal_scale`; `call_stack()[0]
   .function == "internal_scale"`; `locals(0)` has `n`; `globals()` contains
   `pair_hidden` (`"2"`, size 4).
6. `kr232_symbols_pair_cross_unit_global` — `pair_global` is present
   (`i32`, 4, `"40"`), defined in `symbols_pair_math.cpp` and read from
   `symbols_pair_main.cpp`; already green once the fixture exists.

### 7.3 F3 `symbols_asm.kr232`

1. `kr232_symbols_asm_breakpoint_line_equality` — `stop_on_line` on the token
   `add r2, r3`; the stopped source is `symbols_asm.s`, and the line equals
   `line_of_token` for that instruction (a physical line).
2. `kr232_symbols_asm_lines_and_stepping` — `check_line_records_are_in_source`
   for `symbols_asm.s`; `step(LineOver)` from the token `st [r13-4], r2`
   visits the token lines of `ld r2, [r13-4]`, `add r2, r3`, `leave #0`,
   `ret` in order, never leaving the file.
3. `kr232_symbols_asm_functions_and_stack` — `function_at(asm_add.begin)`
   is `asm_add`; `line_at(asm_add.begin)` is the first instruction's physical
   line; at the `add r2, r3` stop `call_stack().size() >= 2`,
   `[0].function == "asm_add"`, `[1].function == "_start"`, and each frame's
   pc is inside its function's span.
4. `kr232_symbols_asm_global_data_objects` — `globals()` contains
   `asm_counter` (size 4, value `"7"`) and `asm_table` (size 12, value
   `"11"`, i.e. the first `.word`); no type is asserted for either because
   assembly declares none (documented deviation from R9 for asm).
5. `kr232_symbols_asm_local` — at the stop on the token `ld r2, [r13-4]`
   (i.e. after the store), `locals(0)` contains `asm_value` (the `.debug_var`
   contract), size 4, value `"2"`, `location.kind == FrameOffset` with offset
   `-4`.

### 7.4 F4 `symbols_include.kr232`

1. `kr232_symbols_include_lines_and_sources` — the only non-crt0 source is
   `include_heavy.cpp`; every line record is within its line count;
   `stop_on_line` on the token `player.power = delta;` succeeds with
   `line_file == include_heavy.cpp`.
2. `kr232_symbols_include_types_and_members` — `info().type_by_name` has
   `Vec`, `Player`, `World`; `World`'s member `player` has offset 0 and type
   `Player`; `Player`'s member `power` has offset 0 and type `i32`. At a stop
   in `main`, `locals(0)` has `world` (`World`, size 4, `has_children`).
3. `kr232_symbols_include_this` — stop in `World::tick`: `locals(0)` has
   `this` (`ptr<World>`) and `delta` = `"4"` (the argument); `call_stack`
   frame 0's source is `include_heavy.cpp`.

### 7.5 F5 startrekng (existing image)

1. `kr232_symbols_startrekng_sources_are_real` — for every `SourceFile` in
   the image whose recorded path resolves to a file, every line record is
   within `1..line_count`. RED now (measured spans above); green after P0/P1
   regenerate the image via `scripts/build-startrekng-kr232.ps1 -Clean`.
2. `kr232_symbols_startrekng_frames_have_sources` — run a bounded session to
   a breakpoint in `galaxy.cpp`; every `call_stack()` frame resolves
   (`function_at(frame.pc)` is non-null and `frame_source` is non-empty), and
   frame 0's source is `galaxy.cpp`. Frames may name example units or the kr232
   runtime; none may be nameless.
3. `kr232_symbols_startrekng_main_script_line` — with a breakpoint on the
   token `game.run()` in `main_script.cpp`, the stop line is that token line
   and the source is `main_script.cpp` (this is the include-heavy real-world
   anchor; `main_script.cpp` includes `game.hpp`/`script.hpp`).

### 7.6 F6 `promoted_o1.kr232` (promotion guarantee at `-O1`)

1. `kr232_symbols_promoted_o1_visible` — at token A `locals(0)` contains
   `doubled` (its value is not asserted before the assignment has run); at
   token B `doubled` is `"20"`; at token C it is `"21"` when `main` calls
   `caller(5)` (which passes `scaled == 10` to `eligible`); the variable is
   listed at all three stops, so it is visible across its live range.
2. `kr232_symbols_promoted_o1_value_updates_while_stepping` — stepping
   `LineOver` from token B to token C changes `doubled` from `"20"` to `"21"`.
3. `kr232_symbols_promoted_o1_uses_a_frame_slot` — every `VariableInfo` of
   `eligible`/`caller` in `session.info().variables` has
   `location.kind == FrameOffset`; none is `LocationKind::Register`. This is
   the producer guarantee that makes the values above trustworthy at `-O1`.
4. `kr232_symbols_promoted_o1_frame1` — at the stop in `eligible`,
   `locals(1)` (caller `caller`) contains `scaled` with value `"10"` (5 * 2);
   frame 1's pc resolves to `caller`; this is R5 for a promotion-candidate
   local read in the caller's context.
5. `kr232_symbols_address_taken_stays_in_memory` — at the token
   `return sink;` in `address_taken`, `sink` is listed with value `"3"` and a
   `FrameOffset` location. A pass may never promote an address-taken local.
6. `kr232_symbols_promoted_o2_unsupported` — **not a krdbg test.** The `-O2`
   policy is a KRCompiler producer test (§8.3,
   `krc_debug_kr232_o2_recorded_promotion`); what krdbg must do for a
   genuinely promoted variable is the §7.7 consumer contract. See §7.6.1.

#### 7.6.1 The `-O2` case

Measured today (pre-Track A): at `-O2` the driver enables
`alloca_promotion(..., promote_recorded = true)`
(`../krcompiler/src/toolchain/toolchain.cpp:685-686`), which may promote a
named `<variable>.addr`, while the debug record still describes a frame
offset. For kr232 the alloca disappears, so the variable disappears from
`variables` (measured). KDBG v1 has no register-location record on the kr232
path, and krdbg neither translates `.kdbg_loc` nor models a live range, so
there is no honest value to show.

The committed design (`docs/register-promoted-locals.md` Track A item 5)
settles this without a format change: kr232 passes
`promote_recorded = false` at every level until Track B's register producer
and krdbg consumer land, so `-O2` keeps every named local in its home slot and
lists it. Then:

- The producer-side policy test is `krc_debug_kr232_o2_recorded_promotion`
  (`docs/register-promoted-locals.md` §4.1): at `-O2` a named local is still
  listed, so the Variables window cannot silently empty again.
- The consumer-side fail-safe rule for genuinely register-resident locals is
  Track B's (§7.7): a variable outside its ranges with an unusable home slot is
  `<optimized out>`, never a stale slot read.
- A short note is added to `docs/debugging-kr232.md` (§ Current limits) so a
  user debugging `-O2` kr232 code knows why a local can be missing.

### 7.7 Register-held locals (R15, Track B)

The consumer is verified without a producer, exactly as
`docs/register-promoted-locals.md` §3.3–§3.5 and §4.2 specify. This plan owns
the rows in §11; the test names below are that document's, so the two plans do
not invent competing versions:

1. `kdbg_translates_location_records` (`tests/test_kdbg.cpp`, which already
   builds KDBG payloads byte by byte): a hand-built v1.1 payload with a
   `V` record and two `Location` records for one variable (one `gpr`, one
   `fpr`, two ranges) must produce `DebugInfo.locations` entries with the right
   function, name, group, index and half-open addresses; a payload without
   locations leaves the vector empty (v1 stays byte-identical).
2. `session_reads_a_register_local_inside_its_range`
   (`tests/test_kr232_symbols.cpp`, using `FakeTarget` from
   `src/core/fake_target.h`, which the kr232 test binary links through
   `krdbg_core`): a synthetic `DebugInfo` whose variable has a `FrameOffset`
   home and a register range, a `FakeTarget` holding different values in the
   slot and in the register; `locals(0)` returns the register value at a PC
   inside the range and the home-slot value outside it.
3. `session_reports_an_optimized_out_local`: with `VariableSlotUnusable` set
   and a PC outside every range, the value is `<optimized out>`, not a slot
   read.
4. `session_register_local_in_a_caller_frame_is_unavailable`: the same
   register variable in frame 1 is reported unavailable (`?`), never the
   current frame's register. `RawFrame` carries no register snapshot
   (`include/krdbg/target.h:97-100`), so "read in the caller's context" for a
   register home is not representable; the maintainer's frame-1 requirement is
   met by the `-O1` home-slot case in §7.6 item 4, and this rule prevents the
   caller frame from showing a lie.
5. `session_members_of_a_register_local_read_the_register`: a register-held
   aggregate expands through the resolved location, not the stale slot.

Until Track B's kr232 producer exists, all of these use synthetic
`DebugInfo`/payload data; no kr232 image can be asserted to contain a
register location.

## 8. Where the verification lives

### 8.1 krdbg (all session-level rows)

Add one source file to the existing kr232 test binary and one CTest row. No
new binary, no golden changes (adding sources to `krdbg_kr232_tests` cannot
shift the UI goldens; that is why the kr232 tests are already separate,
`CMakeLists.txt:337-349`).

```cmake
add_executable(krdbg_kr232_tests
    tests/test_main.cpp
    tests/test_kr232_adapter.cpp
    tests/test_kr232_scenarios.cpp
    tests/test_kr232_symbols.cpp        # new
    tests/test_host_build.cpp
    tests/test_app_smoke.cpp
)
# Target the new rows as one CTest entry so a failure names the surface.
add_test(NAME krdbg_kr232_symbols
         COMMAND krdbg_kr232_tests --filter kr232_symbols_)
set_tests_properties(krdbg_kr232_symbols PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PASS_REGULAR_EXPRESSION "kr232_symbols_(cpp|pair|asm|include|promoted)")
```

`--filter` is a substring match (`tests/test_main.cpp:14-25`); the
`PASS_REGULAR_EXPRESSION` guards against a misnamed filter passing vacuously.
`tests/test_kr232_symbols.cpp` gets the fixture path from
`KRDBG_SOURCE_ROOT`, exactly as `test_kr232_adapter.cpp:18-22` uses its own
definitions; no new compile definition is needed. The payload-level test (the
first row in §7.7) is added to `tests/test_kdbg.cpp`, which is already in
`krdbg_tests` and already builds and parses KDBG payloads by hand.

### 8.2 Patch points in the existing tests

- `tests/test_kr232_adapter.cpp:434-528` (`kr232_adapter_unwinds_two_frames`):
  after P1, add frame-1 values and frame source assertions for
  `nested_frames.kr232` so the older fixture also proves caller context.
- `tests/test_kr232_scenarios.cpp:246-377` (`kr232_scenario_startrekng`):
  add the §7.5 invariants (they are the same session and image the test
  already loads).
- `tests/test_kr232_scenarios.cpp:381-418`: `aggregate.kr232` is regenerated
  in P1; keep the existing size/member assertions and add values
  (`points[0].x == "1"`, `total` after running `main`).

### 8.3 KRCompiler producer-side tests (optional but cheap)

The consumer assertions catch a bad KDBG; a producer test catches it in the
tree that can fix it. These are named only to locate the work, not proposed as
krdbg tests:

- `../krcompiler/tests/unit/test_kr232_assembler.cpp` (currently has the
  bridge test at `:113-161`): a `.debug_line N` directive yields line records
  at N for the following instructions; a non-`.globl` label with
  `.debug_global` still yields a `G` record; a `.globl` data object's `G` size
  is its extent; `F` is emitted for a static function.
- `../krcompiler/tests/unit/test_toolchain.cpp` (has KDBG-decode patterns at
  `:227-330`, `:493-497`): compile a small C++ file and assert every `line`
  lies in `1..source_line_count`, the definition line has a record at the
  function's start address, a `param`/`local` kind exists in `variable_info`,
  a `static` function has an `F` record, a file-scope `static` global has a
  `G` record, and no `argument.copy`/`cond.tmp`/`logic.slot` variable record
  exists.
- `../krcompiler/tests/unit/test_kdbg_v11.cpp`: extend the existing
  `P`/`.kdbg_loc` round-trip (`:94-137`, `:230-250`) with the range-joins-`V`
  case used by §7.7, matching `krc_debug_kr232_location_bridge`.
- Track A's policy tests (`docs/register-promoted-locals.md` §4.1):
  `krc_debug_kr232_local_store_survives_o1` (a recorded slot's store survives
  the `-O1` peepholes), `krc_debug_kr232_o2_recorded_promotion` (`-O2` still
  lists named locals), and `krc_debug_kr232_source_locals` (kinds, no
  temporaries, static-function locals).

### 8.4 Commands

Windows (Developer PowerShell for VS 18; the kr232 presets carry the adapter
and the `../kr232` root):

```powershell
cmake --preset windows-msvc-kr232
cmake --build --preset windows-msvc-kr232
ctest --preset windows-msvc-kr232 -R "kr232" --output-on-failure
# equivalent one-shot build path used by the gate:
powershell -File scripts/build-debugger.ps1 -Clean
ctest --test-dir build/kr232-debugger -R "kr232_symbols|kr232_fixtures" --output-on-failure
```

WSL x64 (build on the ext4 mirror, never on `/mnt/c`; the gate re-executes
from the mirror itself):

```sh
sh ../krcompiler/scripts/kr-mirror.sh krdbg
cd ~/mirrors/krdbg
KRG_PREFIX=/home/kevinro/.local bash scripts/build-debugger.sh
ctest --test-dir build/kr232-debugger -R "kr232_symbols|kr232_fixtures" --output-on-failure
# bare gate still runs beside the adapter build:
bash scripts/gate-linux.sh
```

The family gate (`../kr232/scripts/check-family.ps1`) runs the krdbg gate in
dependency order; the symbol rows must be green there on both platforms before
the symbol work is called done.

## 9. Fixture regeneration and cross-platform rules

`regenerate.ps1` and `CheckFixtureDrift.cmake` must gain the same new entries
in the same order:

```powershell
# regenerate.ps1, after the existing aggregate block. --far-calls is the
# startrekng recipe for cross-unit C++ calls; the asm fixture patches its own
# local call.
Compile-From $repo 'tests/fixtures/kr232/src/symbols_cpp.cpp' "$workText/symbols_cpp.o" @('--far-calls', '-O0')
Compile-From $repo 'tests/fixtures/kr232/src/symbols_pair_math.cpp' "$workText/symbols_pair_math.o" @('--far-calls', '-O0', '-I', 'tests/fixtures/kr232/src')
Compile-From $repo 'tests/fixtures/kr232/src/symbols_pair_main.cpp' "$workText/symbols_pair_main.o" @('--far-calls', '-O0', '-I', 'tests/fixtures/kr232/src')
Compile-From $repo 'tests/fixtures/kr232/src/symbols_asm.s' "$workText/symbols_asm.o"
Compile-From $repo 'tests/fixtures/kr232/src/include_heavy.cpp' "$workText/include_heavy.o" @('--far-calls', '-O0', '-I', 'tests/fixtures/kr232/src/include')
Compile-From $repo 'tests/fixtures/kr232/src/promoted.cpp' "$workText/promoted.o" @('--far-calls', '-O1')
Link-Fixture $repo 'symbols_cpp.kr232' @("$workText/crt0.o", "$workText/symbols_cpp.o")
Link-Fixture $repo 'symbols_pair.kr232' @("$workText/crt0.o", "$workText/symbols_pair_math.o", "$workText/symbols_pair_main.o")
Link-Fixture $repo 'symbols_asm.kr232' @("$workText/symbols_asm.o")
Link-Fixture $repo 'symbols_include.kr232' @("$workText/crt0.o", "$workText/include_heavy.o")
Link-Fixture $repo 'promoted_o1.kr232' @("$workText/crt0.o", "$workText/promoted.o")
```

Rules, all already followed by the existing script and drift check:

- Every compile runs from a root and names the source relative to it with
  **forward slashes**; the source path is embedded in the image. Compiling
  `symbols_cpp.cpp` from the repository root records
  `tests/fixtures/kr232/src/symbols_cpp.cpp`, which resolves under
  `KRDBG_SOURCE_ROOT` on both platforms.
- No absolute paths and no `--source-root` rewriting for these fixtures; the
  recorded path is already repo-relative.
- The image bytes must match on Windows and Linux, so no timestamps, no host
  paths, and identical argument order. `CheckFixtureDrift.cmake` is the
  cross-platform check (`-DKRC=... -DKRLD=... -DSOURCES=...
  -DFIXTURE_DIR=... -DWORK=... -P ...`), and `ctest -R
  krdbg_kr232_fixtures_match_source` runs it when
  `KRDBG_KR232_KRC`/`KRDBG_KR232_KRLD` are set
  (`CMakeLists.txt:478-497`).
- Regeneration is **not** run in CI: the containers are committed, the drift
  check proves they still match their sources with whatever compiler the build
  names. P1 therefore means: rebuild the prefix from the committed KRCompiler,
  run `regenerate.ps1`, review the new bytes, and commit both the compiler pin
  and the images.
- The `tests/fixtures/kr232/README.md` table gains the five new images and
  states that line records for C/C++ are only trustworthy from P0 onward.

## 10. Session-API pitfalls the fixtures avoid

These are real behaviours of the current API, not bugs to fix here; the
fixtures are designed around them so a failure means a symbol problem, not an
API ambiguity:

- `members(const VariableView&)` finds the `VariableInfo` by **name and type
  only** (`src/core/session.cpp:697-704`). Two aggregates with the same name
  and type in different functions would resolve to the first. F1/F2/F4 use
  distinct names per function; where a name repeats the type differs (`counter`
  is a `Counter` in `main`, `this` is a `ptr<Counter>` in `Counter_bump`), so
  `members()` stays unambiguous.
- A pointer or array member's `MemberView` does not carry storage for a second
  expansion (`src/core/session.cpp:754-774` assigns no `storage`), so
  assertions expand struct/array variables and one level of `*`, not
  `this->member`.
- Line breakpoints resolve to the **first** `LineInfo` with the requested
  (source,line) (`src/core/session.cpp:125-141`). Fixture tokens are unique
  lines, so "first" is "the" instruction.
- `VariableView` has no flags; kind assertions read `session.info().variables`.
- `frame_source` uses `line_at(frame.pc)`; a frame whose PC has no record
  (e.g. a static function before P2) fails the check by design — that is the
  symptom being caught.

## 11. Pass/fail matrix

Legend: **PASS** = expected green once the row's prerequisite phase lands;
**RED** = not achievable with today's compiler/krdbg (measured with a probe
where one exists, and blocked on the named phase otherwise), and lands only
with its prerequisite — per §4 no red test is committed before then. Fixture
F5 is optional at runtime (only when the startrekng image exists), like the
existing golden row.

| Req | Fixture | Assertion | Test name | Now | Needs |
|---|---|---|---|---|---|
| R1 | F1 | token-line breakpoint stops on that line/file | `kr232_symbols_cpp_breakpoint_line_equality` | RED | P0,P1 |
| R2 | F1 | LineOver statement sequence, file never changes | `kr232_symbols_cpp_line_stepping_stays_in_source` | RED | P0,P1 |
| R2 | F2 | LineIn crosses units; LineOver does not | `kr232_symbols_pair_line_stepping_into_other_unit` | RED | P0,P1 |
| R3 | F3 | physical-line breakpoint and LineOver sequence | `kr232_symbols_asm_lines_and_stepping` | PASS | P1 |
| R1/R3 | F3 | stopped line == requested line | `kr232_symbols_asm_breakpoint_line_equality` | PASS | P1 |
| R4 | F1 | names+values of params/locals at frame 0 | `kr232_symbols_cpp_locals_frame0` | RED | P0,P1 |
| R4/R9 | F1 | aggregate parameter expands | `kr232_symbols_cpp_aggregate_param` | RED | P0,P1 |
| R5 | F2 | caller-frame values through caller frame | `kr232_symbols_pair_frame1_locals` | RED | P0,P1 |
| R6/R9 | F1 | external globals: type, size, value, members | `kr232_symbols_cpp_globals_and_members` | PASS | P1 |
| R6 | F2 | cross-unit external global | `kr232_symbols_pair_cross_unit_global` | PASS | P1 |
| R7/R8 | F1 | static function frame + static global | `kr232_symbols_cpp_internal_linkage` | RED | P2 |
| R7/R8 | F2 | static function frame + static global | `kr232_symbols_pair_static_function` | RED | P2 |
| R8 | F2 | 2 frames, names, bounds, source per frame | `kr232_symbols_pair_frames_and_sources` | RED | P0,P1 |
| R8 | F3 | asm call stack over ENTER frames | `kr232_symbols_asm_functions_and_stack` | PASS | P1 |
| R4 | F3 | asm frame-relative local through `.debug_var` | `kr232_symbols_asm_local` | PASS | P1 |
| R8 | F5 | frames resolve to example sources | `kr232_symbols_startrekng_frames_have_sources` | RED | P0,P1 |
| R9 | F4 | header-defined types/members survive includes | `kr232_symbols_include_types_and_members` | PASS | P1 |
| R10 | F1 | `this` listed in a member frame | `kr232_symbols_cpp_locals_frame0` | RED | P0,P1 |
| R10 | F4 | `this` for a header-defined class | `kr232_symbols_include_this` | RED | P0,P1 |
| R1/R2 | F4 | include-heavy line table is the .cpp's | `kr232_symbols_include_lines_and_sources` | RED | P0,P1 |
| R2 | F5 | every startrekng line record is a real source line | `kr232_symbols_startrekng_sources_are_real` | RED | P0,P1 |
| R1 | F5 | main_script.cpp token line | `kr232_symbols_startrekng_main_script_line` | RED | P0,P1 |
| R11 | F1 | params carry `VariableParam` | `kr232_symbols_cpp_parameters_are_params` | RED | P2 |
| R12 | F1 | no `argument.copy`/temp names in locals | `kr232_symbols_cpp_no_compiler_temporaries` | RED | P2 |
| R12 | krcompiler | no temporary variable records at all | `krc_debug_kr232_source_locals` | RED | P2 |
| R13/R14 | krcompiler | a recorded slot's store survives `-O1` | `krc_debug_kr232_local_store_survives_o1` | RED | P2 |
| R14 | krcompiler | `-O2` still lists named locals (Track A gate) | `krc_debug_kr232_o2_recorded_promotion` | RED | P2 |
| R13 | F6 | listed and correct across the live range | `kr232_symbols_promoted_o1_visible` | RED | P0,P1,P2 |
| R13 | F6 | value updates while stepping | `kr232_symbols_promoted_o1_value_updates_while_stepping` | RED | P0,P1,P2 |
| R13 | F6 | home-slot guarantee: no register locations | `kr232_symbols_promoted_o1_uses_a_frame_slot` | PASS | P1 |
| R5/R13 | F6 | frame-1 caller local read in caller context | `kr232_symbols_promoted_o1_frame1` | RED | P0,P1 |
| R13 | F6 | address-taken local never promoted | `kr232_symbols_address_taken_stays_in_memory` | RED | P0,P1,P2 |
| R15 | core | `.kdbg_loc` → `DebugInfo.locations` with ranges | `kdbg_translates_location_records` | RED | P4 |
| R15 | core | register value inside the range, slot outside | `session_reads_a_register_local_inside_its_range` | RED | P4 |
| R15 | core | `<optimized out>` when the slot is unusable | `session_reports_an_optimized_out_local` | RED | P4 |
| R15 | core | caller-frame register is unavailable, not a lie | `session_register_local_in_a_caller_frame_is_unavailable` | RED | P4 |
| R15 | core | register-held aggregate expands correctly | `session_members_of_a_register_local_read_the_register` | RED | P4 |

The asm rows are deliberately listed as PASS after P1 because the assembler
already emits physical-line records for hand-written `.s` (measured: 5 records
on the 5 instruction lines of a probe, correct line numbers); they are still
listed under P1 because the fixtures are regenerated then, and because the
surrounding matrix is only meaningful when C/C++ rows are trustworthy too.

## 12. The three highest-risk verifications

1. **Line-table truthfulness for C/C++ (R1/R2, F1/F2/F4/F5).**
   Every local, stack and stepping assertion is anchored on a stop address; if
   the line table is a generated-assembly table under a `.cpp` name, tests can
   pass while the debugger shows the wrong line. Measured today: a 10-line C
   file carries records up to line 29, `aggregate.c` (25 lines) up to 141, and
   `main_script.cpp` (8 lines) up to 240 in the current startrekng image. The
   WIP `.debug_line` fix corrects statement lines but still leaves the prologue
   on generated numbers, so the definition-line-before-prologue fix and the
   `check_line_records_are_in_source` invariant are the load-bearing parts.
   Verify by deliberately regenerating one fixture with the old compiler and
   confirming the new tests go red.
2. **Internal linkage (R7/R8/R11/R12, F1/F2).**
   Static functions and file-scope statics are not just missing labels: a
   static function's line records are dropped by the linker because no `F`
   span covers them, so no breakpoint can bind inside it and no call-stack
   frame can name it. Measured: the symbol dump has no `hidden` function and
   no `file_total` symbol, while the KR32 backend emits `G` records for the
   same program without filtering internal linkage
   (`../krcompiler/src/backend/kr32_backend.cpp:1072-1080`). This is also where
   the producer-side fixes are least mechanical (join `G`/`F` records for
   internal labels; emit `W` kinds for parameters/locals; filter variables to
   `<name>.addr`).
3. **Promotion and live ranges (R13/R14/R15, F6 and §7.7).**
   The home-slot promise is testable now, but the moment a named local is
   promoted (`-O2`, or any future promotion pass) the frame-offset record is
   either stale or absent: measured, the `-O2` image lists only aggregate
   locals and every scalar local vanishes. Nothing in krdbg can read a
   register location (no `.kdbg_loc` translation, no live-range model) and no
   kr232 producer emits one. This is the verification most likely to be
   declared done prematurely, because a `-O0`/`-O1`-only suite stays green
   forever. The committed design `docs/register-promoted-locals.md` closes it
   in two ordered tracks; this plan carries its policy tests and consumer rows
   in the matrix, so "the suite is green" cannot be mistaken for "promotion is
   verified".

## 13. Non-goals

- No change to the KDBG format, `session.h`, or `debug_info.h` in this plan.
- No goldens and no surface pixels: this is a session-level and producer-level
  verification; the existing kr232 UI goldens are unaffected.
- No `-O2` correctness claim for named locals until Track A (`-O2` keeps the
  home slots) and Track B (a register home, the consumer, and its fail-safe
  spelling) are designed and landed (`docs/register-promoted-locals.md`).
- No attempt to cover C (as opposed to C++) separately: the existing
  `aggregate.c`/`kr232_*` fixtures are regenerated in P1 and their existing
  assertions gain values; the C++ fixtures cover the same mechanisms.
- No ARM64-specific symbol run in this plan; the ARM64 gate runs the same CTest
  rows, and the fixtures are host-neutral images.
