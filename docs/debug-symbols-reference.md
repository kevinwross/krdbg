# Debug-symbol reference: what a backend must emit for krdbg

**Status:** reference, derived from the KR32 path by reading the current source of
`../krcompiler`, this repository and `../kr32`, plus the probes listed in
[Evidence](#evidence). Nothing here is a plan; every statement describes code as
it exists.

This page is the specification a rewrite (kr232) follows: which records a backend
must emit into the textual `.debug_info` bridge, which fields each record must
carry, how the linker turns the bridge into KDBG, and exactly what krdbg reads out
of that KDBG payload. KR32 is the model because its backend is the oldest and most
complete producer; kr216 appears only for contrast. The register-promotion half —
the `P`/`Location` mechanism — is in
[§5](#5-register-promotion-and-the-plocation-record).

Path convention: `KC/` is the KRCompiler checkout (`../krcompiler/`), `KD/` is
this repository, `KR32/` is the KR32 VM checkout (`../kr32/`). Every claim cites
`file:line` in the checkout named.

## 1. The pipeline

A backend never writes KDBG. It writes NUL-terminated text records into a
`.debug_info` section (the "bridge"); the linker parses that bridge after layout
and emits the binary `.kdbg` section; krdbg decodes it.

1. **Compile.** `toolchain::compile` builds a backend, calls `emit()`, and keeps
   the backend's text only when `Options::debug` is set. `Options::debug`
   defaults to true (`KC/include/krc/toolchain.h:86`) and there is no CLI switch
   for it in this driver version. KR32: `KC/src/toolchain/toolchain.cpp:770-780`;
   kr216: `:757-768`; kr232: `:734-744`.
2. **Assemble.** `toolchain::assemble` appends the bridge as the `.debug_info`
   section of the object (`KC/src/toolchain/toolchain.cpp:169-181`, KR32 call at
   `:304-309`). KR232 is the exception: its assembler synthesises its own bridge,
   so the option's bridge is not appended (`KC/src/toolchain/toolchain.cpp:282-297`).
   A hand-written `.s` source has no backend bridge: the driver calls
   `assemble(text, ...)` with an empty bridge (`KC/src/tools/krc/main.cpp:366-375`
   for `krc image`, `:811-875` for `-c`).
3. **Link.** krdbg's host chain invokes `krld`, which runs the same linker
   (`KD/src/host/build_chain.cpp:251-272`). `link_objects` concatenates all
   objects' `.debug_info` into one section, renumbering `S` ids and remapping the
   `L` ids that name them (`KC/src/linker/linker.cpp:123-196`, applied at
   `:375-381`). The concatenated text is not shipped: it is the input to
   `attach_kdbg` (`KC/src/linker/linker.cpp:742-1034`; declared
   `KC/src/linker/linker.h:67-68`), which:
   - collects linked symbol values by name (`:758-761`),
   - parses every record (`:800-971`),
   - drops line records that fall outside every function range (`:973-985`),
   - encodes a KDBG payload (`:987-995`) and appends it as section `.kdbg`
     (`:1028-1032`),
   - rewrites the `S` paths in the bridge to the same `--source-root`-relative
     paths written into KDBG (`:997-1031`).
   No `.debug_info` section means no `.kdbg` is attached at all: `attach_kdbg`
   returns true immediately (`:748-756`).
4. **Encode.** `krc::debug::encode_kdbg` writes the KDBG v1.1 payload
   (`KC/src/debug/kdbg.cpp:224-472`): sections are named and typed from one fixed
   array (`:226-228`), and an empty record vector produces no section at all
   (`:413-420`). Only the `Build` section is unconditional (`:404-411`).
5. **Load.** The adapter finds `.kdbg` in the container and decodes it. The KR32
   adapter requires it for a standalone image: `KD/adapters/kr32/kr32_adapter.cpp:483-500`
   (`kdbg::read_section` failure is a load failure at `:493-496`), while OS mode
   tolerates a missing payload on either the kernel or the BIOS image
   (`:502-543`). Decoding is the vendored krc-debug decoder
   (`KD/third_party/krc-debug/src/kdbg.cpp`, same layout as
   `KC/src/debug/kdbg.cpp`), reached through `KD/src/kdbg/kdbg.cpp:233-242`.
   It checks magic, major version, header size, header CRC, section-table bounds,
   string-table bounds, per-section CRC (`KC/src/debug/kdbg.cpp:473-546`) and, when
   the payload records one, the image base against the container's base
   (`:1035-1054`).
6. **Translate.** `KD/src/kdbg/kdbg.cpp:62-218` builds the neutral `DebugInfo`.
7. **Consume.** `DebugSession` (`KD/src/core/session.cpp`) answers line, breakpoint,
   variable and stack queries; the surface calls `line_at`, `locals`, `globals`,
   `members`, `call_stack`, `breakpoints` (`KD/src/ui/surface.cpp:773,865,1030-1072,1670,2064,2713`).

## 2. Record-by-record reference

The bridge is NUL-separated text; `attach_kdbg` reads one record per NUL
(`KC/src/linker/linker.cpp:804-814`) and `stream >>` extraction means every token
is whitespace-delimited. `KC/docs/symbols.md:91-107` is the format's own record
table; the rows below add what KR32 actually writes, which linker line parses it,
which KDBG tag it becomes and where krdbg reads it.

| Tag | KR32 emitter (`KC/src/backend/kr32_backend.cpp`) | KR32's payload | Linker parse | KDBG record | krdbg translation |
|---|---|---|---|---|---|
| `S` | `add_debug_line` `:1285-1293` | `<id> <path>` | `:815-829` | `Source`, tag 1, `.kdbg_src` | `KD/src/kdbg/kdbg.cpp:73-80` |
| `L` | `add_debug_line` `:1294-1296` | `<label> <source-id> <line> <column>` | `:830-838` | `Line`, tag 2, `.kdbg_line` | `:126-138` |
| `F` | `:1498-1510` | `<display-name> <symbol> .L<symbol>.ret <frame> <return-type>` | `:839-850` | `Function`, tag 3, `.kdbg_func` | `:110-124` |
| `G` | `emit` `:1062-1081` | `<name> <label> <type> <size>` | `:851-859` | `Symbol`, tag 4, `.kdbg_sym` | `:140-148`, then `:189-217` |
| `V` | params `:1535-1537`, locals `:1580-1582` | `<function> <name> <frame-offset> <type> <size>` | `:860-864` | `Variable`, tag 5, `.kdbg_var` | `:156-187` |
| `W` | params `:1538-1539`, locals `:1583-1584` | `<function> <name> <kind> <scope> <flags>` | `:921-937` | `VarInfo`, tag 11, `.kdbg_var` | `:150-154`, `:170-185` |
| `K` | `:1511-1515` | `<symbol> <symbol> - <symbol> .L<symbol>.end` | `:904-920` | `Scope`, tag 10, `.kdbg_scope` | not read (dropped at `:62-218`) |
| `P` | never emitted (see §5) | `<function> <name> <reg> <start> <end>` | `:884-903` | `Location`, tag 9, `.kdbg_loc` | not read (dropped at `:62-218`) |
| `T` | `add_debug_type` `:824` | `<name> <size>` | `:865-869` | `Type`, tag 6, `.kdbg_type` | `:82-89` |
| `C` | `:838-840` | `<name> <size> <base-or-minus>` | `:870-883` | `Type` kind `struct` + `Base`, tag 12 | `:82-89`, `:97` |
| `M` | `:841-866` | `<type> <field> <offset> <field-type> <field-size>` | `:954-970` | `Member`, tag 7 | `:101-107` |
| `A` | `:867-870` | `<type> <element-type> <count>` | `:938-946` | `Type` kind `array` + `ArrayType`, tag 13 | `:98-100` |
| `R` | `:871-875` | `<type> <target-type>` | `:947-953` | `Type` kind `pointer` + `TypeRef`, tag 14 | `:98-100` |

A composite type therefore lands as one `Type` record plus a kind and the
sub-records for that kind: `T` creates the entry with kind `builtin`
(`KC/src/linker/linker.cpp:784-798`), `C`/`A`/`R` overwrite the kind, `M` fills
members. The encoder writes the type's `Type`, its `Member`s and then any
`Base`/`ArrayType`/`TypeRef` (`KC/src/debug/kdbg.cpp:342-377`), which is the order
`KC/docs/symbols.md:152-156` states.

### 2.1 `S` — source file

KR32 writes `S <id> <file>` with `id` from 0 per translation unit and `file` the
`ir::DebugLocation::file` recorded by the front end
(`KC/src/backend/kr32_backend.cpp:1287-1293`; location origin
`KC/src/irgen/c_irgen.cpp:422-426`). The linker keys sources by path, gives each
distinct path a globally unique id and remaps every object's `L` ids to it
(`KC/src/linker/linker.cpp:147-189`); with `--source-root=DIR` it rewrites the
path to a `/`-separated relative path under `DIR` when the path is inside it
(`KC/src/linker/linker.cpp:818-828`, `KC/docs/symbols.md:201-205`).

Consumed by krdbg as `SourceFile{id, path, name}` where `name` is the basename
derived from the path (`KD/src/kdbg/kdbg.cpp:73-80`, helper `:16-19`). At load the
KR32 adapter may rewrite a relative path to an absolute one found beside the image
or under a configured source root (`KD/adapters/kr32/kr32_adapter.cpp:42-74`).

`S` is required for line breakpoints to resolve: `find_source` matches an exact
normalized path, then the basename, then a path suffix
(`KD/src/core/debug_info.cpp:230-255`), and breakpoint resolution first looks the
file up and gives up if it is absent (`KD/src/core/session.cpp:125-141`).

The bridge token is one whitespace-delimited word, so a recorded path containing a
space cannot round-trip through `attach_kdbg` (`KC/src/linker/linker.cpp:817`).
KR32's own reader accepts spaces because it reads the rest of the line
(`KR32/src/debug/debug.cpp:150-156`). Use paths without spaces, or fix the parser,
before relying on them.

### 2.2 `L` — source line

KR32 writes `L <label> <source> <line> <column>` (`KC/src/backend/kr32_backend.cpp:1294-1296`).
Two label kinds are used:

- `.L<symbol>.begin`, emitted immediately after the function symbol and before
  the prologue, carries the definition's own line (`:1311-1315`). The label and
  the symbol share an address (ADR 0061, `KC/docs/decisions/0061-function-definition-location.md`),
  so a breakpoint on the definition line binds before the frame exists
  (`KC/docs/symbols.md:73-79`).
- `.Ldbg.<symbol>.<n>`, one per emitted instruction that carries a debug
  location, is written immediately before the instruction (`:1488-1494`).

Records are generated while the function is emitted and *before* the `-O1` text
passes run (`:1084-1105`), so a pass may remove the instruction a label described;
the linker then drops any line record whose resolved address is not inside a
function range (`KC/src/linker/linker.cpp:973-985`) and the format deliberately
does not slide a vanished line onto a neighbour (`KC/docs/symbols.md:183-193`).

The linker resolves the label through the linked symbol table and drops the
record when the label is unknown (`KC/src/linker/linker.cpp:830-838`). KDBG stores
address, source id, line and column (`KC/src/debug/kdbg.cpp:280-291`). krdbg
translates address, source and line only — `LineInfo` has no column
(`KD/include/krdbg/debug_info.h:44-49`; translation `KD/src/kdbg/kdbg.cpp:126-138`)
— then sorts by address (`:135-138`) and sets `LineInfo::function` to the
function containing the address (`:43-55`, called at `:132`).

krdbg's line lookup is "the last line record at or before the pc, clamped to the
enclosing function" (`KD/src/core/debug_info_util.h:15-18`,
`KD/src/core/debug_info.cpp:175-228`). When no function contains the pc it falls
back to the last line record at or before the pc without a function check
(`KD/src/core/debug_info.cpp:181-191`). Breakpoints pick the first line
record matching the requested (source, line) in address order
(`KD/src/core/session.cpp:133-140`), i.e. the lowest address when one line has
several statements. Line stepping compares (source id, line) keys and, for
line-over, also requires the same `LineInfo::function`
(`KD/src/core/session.cpp:28-41,484-519`).

### 2.3 `F` — function

KR32 writes
`F <display-name> <symbol> .L<symbol>.ret <frame-size> <return-type>`
(`KC/src/backend/kr32_backend.cpp:1498-1510`):

- `display-name` is `function.debug_name` when set, else the symbol; C++ members
  get `Class::method` while the symbol stays mangled (`:1500-1501`;
  ADR 0030, `KC/src/irgen/c_irgen_setup.cpp:1462-1469`).
- `symbol` is the function symbol and doubles as the **start label**.
- `.L<symbol>.ret` is the **end label**: it sits before the epilogue, so the
  function's half-open `[start, end)` range covers the body and excludes the
  epilogue (`:1588-1609`).
- `frame-size` is the emitted frame size, `layout.frame_size`
  (`:1502-1503`; layout `:302-373`). It is not a frame offset in bytes from the
  symbol: it is the ABI value the unwinder adds to `R7` to reach the saved caller
  frame, because `PUSH R7; ADJSP #-frame; LEA R7,[SP]` places the saved `R7` at
  `R7 + frame` and the return PC at `R7 + frame + 4` (`:1328-1346`;
  `KD/include/krdbg/debug_info.h:57-60`; consumer
  `KD/adapters/kr32/kr32_adapter.cpp:86-95`).
- `return-type` is the IR return-type spelling with `%` removed and spaces
  replaced by `_` (`:1504-1510`).

The linker resolves both labels against the linked symbol table and drops the
whole record if either is unknown (`KC/src/linker/linker.cpp:839-850`). KDBG keeps
name, start, end, frame and type (`KC/src/debug/kdbg.cpp:292-308`); for KR32 the
32-bit `Function` tag is used because `pointer_width` is 4
(`KC/src/linker/linker.cpp:768-769`).

krdbg translates name, start, end and frame; it sets `FunctionInfo::source` and
`::line` to zero and ignores the return type (`KD/src/kdbg/kdbg.cpp:110-124`),
then stable-sorts functions by start (`:121-124`). `F` is what makes the rest of
the debugger work:

- `function_at(pc)` finds the function whose `[begin, end)` contains the pc
  (`KD/src/core/debug_info.cpp:154-173`); line-at clamping and line-over stepping
  depend on it (`:175-228`, `KD/src/core/session.cpp:484-519`).
- `locals(frame)` requires the frame's pc to belong to a function and then
  matches variables by function name (`KD/src/core/session.cpp:654-668`).
- the KR32 unwind loop stops as soon as `find_function(pc)` fails
  (`KD/adapters/kr32/kr32_adapter.cpp:81-84`), so a frame outside every `F` range
  ends the stack — including the innermost frame. A pc in the epilogue therefore
  has no frames at all.
- line-over's "step over a call" fallback detects entry into a callee by a change
  of `function_at` (`KD/src/core/session.cpp:406-437`).

One address detail: the emitter's `.L<symbol>.end` label (after the epilogue and
before the `.cpp_except` descriptor, `KC/src/backend/kr32_backend.cpp:1609-1626`)
is *not* the `F` end. It is only used by the `K` scope, so a scope can be wider
than the function range (observed on the C++ probe: scope `[0x10000,0x100a0)`,
function `[0x10000,0x10094)`; see [Evidence](#evidence)).

### 2.4 `G` — global data symbol

KR32 writes `G <name> <label> <type> <size>` for every module global that is not
an external declaration (`KC/src/backend/kr32_backend.cpp:1062-1081`): external
globals are skipped (`:1063-1064`), internal-linkage (`static`) globals are
included, `name` and `label` are both the symbol name, `type` is the element-type
spelling with `%` stripped and spaces replaced (`:1072-1080`), and `size` is the
type's byte size (`:1067-1071`). The record is emitted after all functions, but
bridge order does not matter to the linker.

The linker resolves `label` through the symbol table and drops the record when it
is unknown (`KC/src/linker/linker.cpp:851-859`). KDBG stores it as a `Symbol`
(`KC/src/debug/kdbg.cpp:309-323`). krdbg does two things with symbols: it keeps a
`SymbolInfo` table (`KD/src/kdbg/kdbg.cpp:140-148`) that nothing outside merging
reads (`KD/src/core/debug_info.cpp:137-142`), and it exposes each symbol with
non-zero size, not matching a function name and not already declared as an
absolute variable, as an **absolute global** `VariableInfo`
(`KD/src/kdbg/kdbg.cpp:189-217`): `LocationKind::Absolute`, address = symbol
address, `flags = VariableGlobal`, size from the symbol. Global listing is
`KD/src/core/session.cpp:671-685`; values are read by
`KD/src/core/variables.cpp:84-86`.

Consequences a producer must honour:

- A `G` with `size == 0` is ignored by krdbg (`KD/src/kdbg/kdbg.cpp:192-194`), so
  the size token must be the real byte size.
- The type name must have a `T` record with the right size or the variable shows
  its type as `?` and formats as a signed integer (see §3.4).
- The label is the bare symbol name and the linker keys symbols by name
  (`KC/src/linker/linker.cpp:758-761`). Two internal-linkage objects with the same
  name therefore resolve to one address: verified with two `counter` statics, both
  `G` records resolved to the first object's `0x100b4` while the second object's
  symbol was `0x100b8`. krdbg then lists the name once, at the first address
  (`KD/src/kdbg/kdbg.cpp:200-207`, which skips a duplicate empty-function name).
  Unique per-object labels are required for correct internal-linkage globals.

### 2.5 `V` — variable frame location

KR32 writes `V <function> <name> <frame-offset> <type> <size>`
(`KC/src/backend/kr32_backend.cpp:1535-1537` for parameters, `:1580-1582` for
locals):

- `function` is the **symbol** (`function.name`), not the display name.
- `name` is the parameter name, or the source variable name recovered by stripping
  the IR suffix `.addr` (`:1559-1568`).
- `frame-offset` is `layout.slots[value]`, a non-negative byte displacement from
  `R7` (`:302-359`, prologue `:1328-1346`). This is the sign convention krdbg
  expects: it sign-extends the u32 to a signed 64-bit `frame_offset` and the
  adapter adds it to the frame base (`KD/src/kdbg/kdbg.cpp:163-168`;
  `KD/src/core/variables.cpp:78-83`; KR32 frame base = `R7`,
  `KD/adapters/kr32/kr32_adapter.cpp:86,89-90`). KR16 and kr216 instead write the
  `frame_size - slot` ("fp minus offset") form; the KR16 adapter negates it back
  at load (`KC/src/backend/kr16_backend.cpp:1195-1203`,
  `KD/adapters/kr16/kr16_adapter.cpp:680-685`). A rewrite should either follow
  KR32's positive-from-frame-base convention or supply the matching adapter
  fix-up.
- `type` is the type spelling normalised like `G`.
- `size` is the object's byte size from the IR type (`:1525-1529`, `:1554-1558`).

The linker copies `V` unchanged (`KC/src/linker/linker.cpp:860-864`); KDBG stores
it in `.kdbg_var` as `Variable` (`KC/src/debug/kdbg.cpp:324-332`). krdbg sets
`VariableInfo::function`, `::name`, `::type`, `::size` and a
`LocationKind::FrameOffset` location; it defaults flags to `VariableGlobal` when
`function` is empty and `VariableLocal` otherwise (`KD/src/kdbg/kdbg.cpp:156-169`).
`W` then overrides the kind and supplies the scope (`:170-185`).

The variable is listed by `locals(frame)` only when `variable.function` equals the
enclosing `FunctionInfo::name` and the variable is not global
(`KD/src/core/session.cpp:660-667`). **`V.function` and `F.name` must therefore be
the same string.** KR32 writes the mangled symbol in `V` and the display name in
`F`, so C++ member-function parameters and locals are not listed by krdbg — see
§6.1. Values are read from `frame_base + frame_offset`
(`KD/src/core/variables.cpp:78-83`).

### 2.6 `W` — variable kind, scope and flags

KR32 writes `W <function> <name> <kind> <scope> <flags>` with `kind` = `param` or
`local`, `scope` = the function symbol, and `flags` literally `0x0`
(`KC/src/backend/kr32_backend.cpp:1538-1539,1583-1584`). `W` is the record that
tells a debugger what `V` alone cannot: parameter vs local, lexical scope, and the
`0x` flag word (`KC/docs/symbols.md:101`: bit 0 address-taken, bit 1 read-only,
other bits reserved).

The linker accepts only `param`, `local` or `static`; anything else drops the
record, and it parses `flags` with `strtoul(..., 0)` so `0x0` is decimal 0
(`KC/src/linker/linker.cpp:921-937`). KDBG stores it as `VarInfo` in `.kdbg_var`
(`KC/src/debug/kdbg.cpp:333-341`).

krdbg builds a `(function, name) -> VarInfo` map and applies the kind as the
`VariableFlag` (`KD/src/kdbg/kdbg.cpp:150-154,170-185`): `Parameter` becomes
`VariableParam`, `Static` becomes `VariableStatic`, `Local` becomes `VariableLocal`;
the scope string is copied into `VariableInfo::scope` (`:173`) and the W flags word
is **not read at all**. Without `W` every `V` is listed as a local
(`:169`), which is what a backend that omits `W` should expect. KR32's own reader
does not parse `W` at all (`KR32/src/debug/debug.cpp:150-243`; stated
`KC/docs/symbols.md:148-150`), so `W` exists for KDBG consumers such as krdbg.

### 2.7 `K` — lexical scope

KR32 writes one function-level scope per function:
`K <symbol> <symbol> - <symbol> .L<symbol>.end`
(`KC/src/backend/kr32_backend.cpp:1511-1515`). Parent is `-` (no parent), the scope
name equals the function symbol, and the range is `[function symbol,
.L<symbol>.end)` — note the end label is after the epilogue, so the scope is wider
than the `F` range. Nested source scopes are not emitted.

The linker resolves both labels and drops the record when either is unknown
(`KC/src/linker/linker.cpp:904-920`); KDBG stores `Scope` in `.kdbg_scope`
(`KC/src/debug/kdbg.cpp:395-403`).

**krdbg does not read scopes**: `translate` never touches `image.scopes`, and
`DebugInfo` has no scope table (`KD/src/kdbg/kdbg.cpp:62-218`,
`KD/include/krdbg/debug_info.h:105-119`). A backend may omit `K` with no effect on
krdbg, but it must emit the `.L<symbol>.end` symbol if it emits `K`, or the linker
silently drops the record.

### 2.8 `P` — register location

KR32 emits none. The record exists in the format and the linker parses it, but no
backend produces it; see [§5](#5-register-promotion-and-the-plocation-record).

### 2.9 `T`, `C`, `M`, `A`, `R` — types

`add_debug_type` (`KC/src/backend/kr32_backend.cpp:809-876`) runs for every type
attached to a parameter, a local, or a global (`:1530`, `:1575`, `:1066`), and
recursively for a composite's base, element or member types (`:836`, `:864`,
`:868`, `:872`). Type names are the IR spelling with `%` removed and spaces
replaced by `_` (`:813-815`), so scalar types are named `i8`/`i32`/`i64`/`f32`/`f64`,
arrays `[4_x_i32]`, pointers `ptr<i32>`, structs their bare tag
(`KC/src/ir/ir.cpp:424-457`). A type's records are emitted depth-first: the
element/base's records come before the composite's (`:831-875`).

- `T <name> <size>` — every named type. `size` is `size_of`
  (`KC/src/backend/kr32_backend.cpp:159-198`).
- `C <name> <size> <base|->` — structs only, with `type->base` or `-`
  (`:831-840`). Base types are emitted as `T` first (`:835-837`). Unions get only
  `T` (the `if` chain has no union branch, `:831-875`), so a union reaches krdbg as
  kind `builtin` and is not expandable.
- `M <type> <field> <offset> <field-type> <field-size>` — one per struct field,
  with offsets computed by aligning each field (`:841-866`), field type normalised
  as above and field size from `size_of`.
- `A <type> <element-type> <count>` — arrays (`:867-870`).
- `R <type> <target-type>` — pointers (`:871-875`).

The linker creates each type on its first `T` with kind `builtin` and overwrites
the kind on `C`/`A`/`R` (`KC/src/linker/linker.cpp:784-798,870-883,938-953`);
`M` adds members, de-duplicated by field name (`:954-970`). Note that `M`'s field
size token is stored (`KdbgMember.size`, `KC/include/krc/debug/kdbg.h:93-98`) but
krdbg ignores it. The encoder emits the type's JSON-equivalent shape as one
`Type` record plus `Member`s and any `Base`/`ArrayType`/`TypeRef`
(`KC/src/debug/kdbg.cpp:342-377`).

krdbg maps the kind strings to `TypeKind` (`KD/src/kdbg/kdbg.cpp:21-41`), records
`base`, `element`/`element_count` and members, resolving member type names in a
second pass (`:82-108`). What the variables code needs from a type:

- a correct `size` for every type that can be a member, an array element or a
  pointer target, because member expansion computes byte ranges from
  `member_type->size` (`KD/src/core/variables.cpp:165-173`) and array expansion
  uses `element->size` (`KD/src/core/session.cpp:720-733`);
- `element`/`element_count` (from `A`) and `element` for pointers (from `R`)
  because array and pointer expansion is driven by them
  (`KD/src/core/session.cpp:720-775`; scalar formatting uses
  `TypeKind::Pointer`, `KD/src/core/variables.cpp:98-105`);
- members (from `M`) because `members()` displays them
  (`KD/src/core/variables.cpp:152-190`).

Inheritance is translated (`type.base` -> `TypeId`,
`KD/src/kdbg/kdbg.cpp:97`) but never used when expanding members, so a derived
object shows only its own fields; KR32's own reader merges base members
(`KR32/src/debug/debug.cpp:95-116`) and krdbg has no equivalent.

`TypeKind::Enum` exists in the neutral model (`KD/include/krdbg/debug_info.h:18`)
but the bridge has no enum record and the linker never sets kind `enum`
(`KC/src/linker/linker.cpp:865-953`), so no KDBG producer reaches it; enum values
format as signed integers (`KD/src/core/variables.cpp:106-113,140-149`).

## 3. What krdbg actually needs

### 3.1 Source-line stepping and breakpoints

| Requirement | Why | Citation |
|---|---|---|
| `S` for every file that appears in an `L` | breakpoint resolution looks up the file first; a missing source never resolves | `KD/src/core/session.cpp:125-141`; `KD/src/core/debug_info.cpp:230-255` |
| `L` per instruction with a source location, plus one at the function entry label | `line_at` picks the last record at or before the pc | `KD/src/core/debug_info.cpp:175-228` |
| `F` covering every `L` address | the linker drops out-of-range lines; line-over compares `LineInfo::function`; `function_at` drives the source pane | `KC/src/linker/linker.cpp:973-985`; `KD/src/core/session.cpp:484-519,838-844` |
| `LineInfo::function` — derived by krdbg from `F`, not a record | distinguishes stepping out of/into a function | `KD/src/kdbg/kdbg.cpp:43-55,132` |
| `F.start ==` the definition-line `L` address | a breakpoint on the definition line binds before the prologue | `KC/docs/symbols.md:73-79`; `KC/docs/decisions/0061-function-definition-location.md` |

The minimum for a line breakpoint is one `S` and one matching `L`; everything
else degrades rather than breaks: with no `F`, `line_at` falls back to the last
line at or before the pc (`KD/src/core/debug_info.cpp:181-191`), but stepping and
the call stack lose their function association.

### 3.2 Parameters and locals

For a variable to appear in the Variables pane (`session->locals(selected_frame)`,
`KD/src/ui/surface.cpp:1030-1031`) it needs:

1. `F` for the frame's pc, because `locals()` returns nothing without an enclosing
   function (`KD/src/core/session.cpp:654-658`).
2. `V.function` equal to `FunctionInfo::name`, i.e. the same name string `F`
   wrote, and `V` not flagged global (`KD/src/core/session.cpp:660-667`).
   KR32 writes the mangled symbol here, which is why C++ member functions lose
   their locals (§6.1).
3. A frame offset consistent with the adapter's frame base
   (`KD/src/core/variables.cpp:78-83`).
4. `W` to say `param` vs `local`; without it every variable is a local
   (`KD/src/kdbg/kdbg.cpp:169`). `W` flags are ignored; `W` scope is copied but
   never read.
5. `T` for the variable's type (`V` carries the type name) for display: without a
   matching type the pane shows `?` as the type name and formats the raw bytes as
   a signed integer (`KD/src/core/session.cpp:178-203`;
   `KD/src/core/variables.cpp:93-150`).
6. `V.size` (or a `T` size) because storage reads use the size; a zero size and a
   non-register location make the read fail (`KD/src/core/variables.cpp:47-50`).

`VariableInfo::decl_line` and `::scope` are never populated from KDBG in a way the
session uses: `decl_line` stays 0 (`KD/src/kdbg/kdbg.cpp:157-168`) and `scope` is
stored but never read by the session or surface (`KD/include/krdbg/debug_info.h:92-101`;
grep of `KD/src` finds no reader outside `translate`).

### 3.3 Globals

`globals()` lists every `VariableInfo` that is flagged global or has no function
(`KD/src/core/session.cpp:671-685`). In the KDBG path those come only from `G`
records turned into absolute variables (`KD/src/kdbg/kdbg.cpp:189-217`). So a
global needs `G` with a resolvable label, a non-zero size and a `T` for its type;
without a type it is listed but reads as an integer and cannot expand. A `G` whose
name equals a function's name is skipped (`KD/src/kdbg/kdbg.cpp:195-199`). The
first `G` of a given name wins (`:200-207`), so duplicate records do not duplicate
the row; they do make the address whatever the first resolvable symbol was
(§6.2).

### 3.4 Aggregates and types

For a struct/array/pointer variable to expand, the KDBG type table needs the
records described in §2.9 with correct sizes. `members()` reads the variable's
bytes and slices them by member offset and member type size
(`KD/src/core/variables.cpp:152-190`), arrays index by element size and count
(`KD/src/core/session.cpp:720-753`), and a pointer dereference reads the pointed
type's size (`:754-775`). A member whose type has no `T` record gets size 0, so it
prints `?` (`KD/src/core/variables.cpp:165-173`). A struct with no `M` records
still renders `{...}` with `has_children == true`, because the flag comes from the
kind (`KD/src/core/session.cpp:190-196`), but `members()` expands to nothing
(`KD/src/core/variables.cpp:152-190`).

### 3.5 Call stack and unwind

The adapter decides how frames are found; KDBG supplies the two numbers it needs.
For KR32 the current `R7` is the frame base, and for each frame the adapter reads
the saved frame pointer at `R7 + F.frame` and the return PC at `R7 + F.frame + 4`,
then continues at the caller's pc (`KD/adapters/kr32/kr32_adapter.cpp:76-98`).
`F.frame` must be the emitted frame size and both words must be at those
displacements, or the stack is wrong. The walk stops when the pc names no
function, a word cannot be read, or the return PC is zero (`:81-96`). The session
then labels each frame with `FunctionInfo::name` for the frame's pc
(`KD/src/core/session.cpp:626-643`), so `F` ranges must cover return addresses —
i.e. a call site's return address must lie inside the caller's `[start, end)`.

`F` also drives the step modes: instruction-over treats a function change as
entering a call (`KD/src/core/session.cpp:406-437`) and line-out/step-out use the
unwind list (`:520-543`).

### 3.6 Contexts

KDBG has no context record. Contexts are supplied by the adapter: the KR32 adapter
reports one `Core 0` context (`KD/adapters/kr32/kr32_adapter.cpp:150-159`) and
`DebugSession` proxies `contexts()`/`select_context()` to the target
(`KD/src/core/session.cpp:307-318`). Nothing a backend emits affects the context
list; it affects only names, lines, variables and stacks inside it.

## 4. What krdbg ignores or tolerates missing

Verified against `KD/src/kdbg/kdbg.cpp:62-218` and by loading hand-built payloads
(§Evidence):

| Data | krdbg behaviour |
|---|---|
| `KdbgScope` (bridge `K`) | decoded, then discarded; `translate` never reads `image.scopes` |
| `KdbgLocation` (bridge `P`) | decoded, then discarded; `translate` never reads `image.locations` |
| `KdbgVarInfo::flags` (`W` flags word) | parsed, not read; the `VariableFlag` comes from `kind` only |
| `W` kind `static` | maps to `VariableStatic`; no producer emits it for KR32 |
| `KdbgFunction::type` (`F` return type) | discarded (`FunctionInfo` has no type field) |
| `KdbgLine::column` | discarded (`LineInfo` has no column) |
| `KdbgSource::name` | derived from the path; the record carries only id and path |
| `KdbgMember::size` | discarded; member size comes from the member type's `T` size |
| `KdbgType::base` | resolved to a `TypeId`, never used for expansion |
| `KdbgImage::minor_version` | not checked; v1 and v1.1 payloads both decode |
| `KdbgImage::target_id`, `::abi_version`, `::build_id`, `::compiler` | copied into `DebugInfo`, read by nothing in `KD/src` or `KD/src/ui` |
| `DebugInfo::symbols` | populated from every `Symbol` record, read only by merge |
| `VariableInfo::decl_line` | always 0 in the KDBG path |
| `VariableInfo::scope` | stored, never read |
| missing `W`, `K`, `P`, `A`, `R`, `G` | tolerated: fewer flags/scopes/globals/types, no failure |
| missing `L`/`F` | tolerated by krdbg core but the pipeline degrades (no lines, or lines with no function association); the linker drops orphan lines anyway |
| missing `S` | tolerated; line breakpoints simply never resolve |
| only a `Build` record (empty payload) | decodes and translates to an empty `DebugInfo`; krdbg runs with no symbols |
| missing `.kdbg` section in a standalone KR32 image | **not tolerated**: the adapter fails the load (`KD/adapters/kr32/kr32_adapter.cpp:493-496`) |
| bad magic/major/CRC/table bounds/image base | **not tolerated**: decode fails (`KC/src/debug/kdbg.cpp:473-546,1035-1054`) |

Confirmed with hand-built payloads: a KDBG image carrying only `S`+`L`+`F` decodes
and drives `function_at`, `line_at` and one frame with zero locals/globals; an
image carrying only `S`+`L` still reports a line at a pc but no function
(§Evidence).

## 5. Register promotion and the `P`/`Location` record

This section grounds the maintainer's note that KR32 and its compilers coped with
register promotion so promoted locals still appeared in the Variables window. In
the current krc and krdbg code the mechanism is **slot stability**, and the
`P`/`Location` machinery is a format the linker carries and the KR32 reader
consumes, with no producer in krc today. The
companion audit [register-promoted-locals.md](register-promoted-locals.md) covers
the historical KR32 producer, the kr232 redesign and the test plan; this section
records only the record format and the consumer contract a rewrite must match.

### 5.1 What krc's KR32 backend promotes

`compute_allocation` runs only when optimising (`-O1` and above)
(`KC/src/backend/kr32_backend.cpp:1629-1634`) and keeps only values its filter
accepts (`:1675`). The filter in the register allocator
(`KC/src/backend/register_allocator.cpp:252-334`) accepts an **instruction
result** that: is a ≤32-bit integer or pointer (`:270-277`); is not an alloca or
`phi` (`:278-281`); has every use in its defining block (`:282-301`); and has no
call between definition and last use (`:302-330`). Parameters are interned for
liveness (`:60-62`) but are not instructions, so `keep_local_allocations` never
keeps one. A second pass additionally keeps up to three entry-block scalars in
callee-saved `R8`-`R10` (`KC/src/backend/kr32_backend.cpp:1680-1811`), again only
instruction results.

So "register promotion" in krc's KR32 backend means *temporaries leave their
slots*: `load_value`/`store_value` take a register path for values in
`allocated_registers_` instead of a frame access
(`KC/src/backend/kr32_backend.cpp:445-455,473-485`), and `layout_function` omits
the slot for exactly those values (`:344-352`). Named source variables are IR
memory objects named `<variable>.addr`; cross-block promotion always refuses
those (`KC/src/ir/mem2reg.cpp:93-98,320`), and single-block promotion refuses them
until the driver passes `promote_recorded`, which it sets only at `-O2`
(`KC/src/toolchain/toolchain.cpp:685-686`; `KC/src/ir/passes.cpp:333-341,682,702`).
Slot omission can never touch a source variable because a source variable is not
an SSA value the allocator kept
(`KC/docs/symbols.md:195-199`; `KC/docs/optimize.md:415,417`;
`KC/docs/plan-o1-debugger.md:42-46`). The refusal is direct in the passes:
`is_recorded_variable` excludes a `<variable>.addr` alloca from promotion
(`KC/src/ir/passes.cpp:324-341`).

### 5.2 Why named source variables keep a stable frame home

`layout_function` reserves a slot for every parameter unconditionally
(`KC/src/backend/kr32_backend.cpp:325-330`), gives each `alloca`'s object its own
byte offset in `alloca_offsets` (`:334-339`), and gives every instruction result the
allocator did not keep a frame slot (`:332-359`). `V`/`W` are then emitted for each
parameter (`:1516-1540`) and for each `<name>.addr` alloca that is not a compiler
temporary (`:1541-1586`, temporary filter `:1559-1568`: `call.temp`,
`operator.temp`, `subscript.this` and anything else without the `.addr` suffix are
skipped). Those same records are emitted at every optimisation level because
`emit_function` does not consult `optimizes(level_)` for them. At `-O0` and `-O1`
the result is one slot per named variable for the whole function, so `V`'s frame
offset is true at every pc and the Variables window needs no range: verified at
both levels on the same C++ probe — the same four `V` records existed and
`locations` was empty in both (`--dump-symbols --json`). krdbg's host chain builds
sources at `-O0` by default (`KD/src/host/build_chain.h:21-23`,
`KD/src/core/tool_settings.h:15`). At `-O2` single-block promotion may move a
named local out of its slot (`KC/src/toolchain/toolchain.cpp:685-686`), and its
`V` record then names a slot nothing writes — the gap ADR 0052 describes and the
reason a `P` record or an "optimized out" row is required before `-O2` debug info
is trustworthy (`KC/docs/decisions/0052-kdbg-variable-scope-ranges.md:27-41`).

### 5.3 The `P` record and KDBG `Location`

The format defines the record; krc does not produce it:

- Bridge spelling: `P <function> <name> <reg> <start_label> <end_label>`, `reg`
  `R0`-`R15` or `F0`-`F15`, an `F` prefix setting the fp flag, labels resolved
  through the linked symbol table, range half-open (`KC/docs/symbols.md:97-99,106-107`).
- Linker parse: `KC/src/linker/linker.cpp:884-903`; it drops the record when a
  label is unknown, sets `location.fp` from an `F`/`f` prefix and parses the
  register number after the first character.
- KDBG encoding: `Location` tag 9 in `.kdbg_loc` (or tag 18 `Location64`),
  payload function, name, register `u16`, fp `u8`, start and end
  (`KC/docs/symbols.md:114-116,125-130`; encoder `KC/src/debug/kdbg.cpp:378-394`);
  start/end are absolute in the load space, half-open.
- No krc backend writes one: `kr32_backend.cpp` has no `P` emission (its
  `debug_info_ +=` sites are exactly `S`,`L`,`F`,`K`,`V`,`W`,`G`,`T`,`C`,`M`,`A`,`R`
  at `:1291,1294,1502,1513,1535,1538,1580,1583,1072,824,838,854,869,873`);
  kr16 and kr216 likewise; the comments and ADRs say so
  (`KC/src/linker/linker.cpp:884-886`; `KC/docs/decisions/0009-kdbg-v1-freeze.md:63-64`;
  `KC/docs/decisions/0052-kdbg-variable-scope-ranges.md:22-23`;
  `KC/docs/optimize.md:80-83`).
- krdbg decodes it but drops it: the vendored decoder fills `KdbgLocation`
  (`KD/third_party/krc-debug/src/kdbg.cpp:628-651`), and `translate` never reads
  `image.locations` (`KD/src/kdbg/kdbg.cpp:62-218`). The neutral model has no
  range on a variable: `VariableInfo` carries one `Location` and no start/end
  (`KD/include/krdbg/debug_info.h:92-101`), and the only `LocationKind::Register`
  producer today is the fake debug info used by tests
  (`KD/src/core/fake_debug_info.cpp:86,127,157,167`).

### 5.4 `P`, `V` and `W` interact additively

A promoted variable is still a source variable: it keeps the `V` record (type,
size, frame offset) and the `W` record (kind, scope, flags), and gains one `P`
record per pc range in which it lives in a register. The reader keeps `V` as the
fallback location and uses `P` only while the pc is inside its half-open range.
That is how KR32's own debugger works: it parses `P`
(`KR32/src/debug/debug.cpp:183-200`), `location_at` linear-scans
`function`/`name`/range (`:717-727`), and `read_variable` prefers the register
inside the range and falls back to `machine.reg(7) + variable.offset` outside it
(`:729-762`). Its struct and comment say the same (`KR32/include/kr32/debug/debug.h:47-56`,
`KR32/docs/debug.md:40-51`: whole-function promotion takes its range from the
function's own `F`-record labels; a span promotion names the labels that bracket
its interval; the companion audit records the same contract and the historical
producer's defects). The KR32 reader's own documentation describes an optimiser
that rewrites the stores of a promoted named variable (`KR32/docs/debug.md:40-51`);
`-O0`/`-O1` do not do that (§5.1), but `-O2` single-block promotion can leave a
`V` slot stale, which is the ADR 0052 gap. KR32's historical compiler did emit `P`
records — see the companion audit's §1.2 for that path and its bugs.

### 5.5 What krdbg must implement to read a promoted variable

Today krdbg shows a promoted variable by reading its frame slot, which is correct
only while the slot is the variable's home. To honour a `P` record it needs:

1. **Carry the ranges.** Either a `DebugInfo::locations` vector (function, name,
   fp, reg, start, end) or a range list on `VariableInfo`. `translate` must copy
   `image.locations` instead of dropping them
   (`KD/src/kdbg/kdbg.cpp:62-218`; `KC/include/krc/debug/kdbg.h:49-58,133`).
2. **Resolve the active location at the current pc.** Given the frame's function
   name and the variable name, pick the `P` range with `start <= pc < end`. The
   KR32 reader does exactly this by scanning (`KR32/src/debug/debug.cpp:717-727`);
   with disjoint ranges at most one matches.
3. **Map the register to the target's value model.** krdbg's register location is
   a `(group, index)` pair (`KD/include/krdbg/debug_info.h:22-28`), read through
   `IDebugTarget::read_value` (`KD/src/core/variables.cpp:53-77`). The KR32 adapter
   names its groups `gpr` and `fpr`, where the index is the register number
   (`KD/adapters/kr32/kr32_adapter.cpp:193-244,585-676`), so a `P` record's
   `fp`/`reg` maps to `Location{Register, group = fp ? "fpr" : "gpr", index = reg}`.
   A target without a matching group returns false and the value shows as `?`
   rather than reading the wrong address.
4. **Fall back outside the range.** Use `V`'s `FrameOffset` when no range matches,
   and if the backend marks a variable as never-in-memory, report it unavailable
   rather than stale (`KC/docs/plan-o1-debugger.md:125-136`, W5). The KR32 reader
   falls back to the slot (`KR32/src/debug/debug.cpp:739-752`).
5. **Apply it in every value path.** `make_view` constructs the displayed location
   from `VariableInfo` (`KD/src/core/session.cpp:171-205`) and `members()`
   re-derives the location for a named variable (`:697-777`); both would need the
   pc-selected location, as would the `VariableView::location` handed to the
   surface.
6. **Do not require `P` for correct values at `-O0`/`-O1`.** Because named
   variables keep their slots there (§5.2), the current KR32 `-O0`/`-O1` images are
   correct without it. It becomes mandatory when a pass lets a named variable leave
   its slot — `-O2` single-block promotion today, and the wider change ADR 0052
   gates.

If the kr232 rewrite keeps promotion to temporaries as KR32 does, the minimal
contract is the same: stable slot + `V`/`W` for every named variable and no `P`.
If it promotes named variables, it must emit one `P` per register-resident range
with resolvable start/end labels, keep `V`/`W` for type/size/kind, and krdbg must
gain the six points above. [register-promoted-locals.md](register-promoted-locals.md)
covers the kr232 redesign in depth: the `.debug_varinfo`/`.debug_scope`/
`.debug_location` directives, the producer's range rules, the precedence and
fallback table, and the tests that pin them.

## 6. KR32 behaviours confirmed by probe (and what a rewrite should note)

### 6.1 C++ member functions lose their locals in krdbg

KR32 writes the mangled symbol in `V`/`W` (`KC/src/backend/kr32_backend.cpp:1535-1539`),
`F` carries the display name (`:1500-1503`), and krdbg matches
`variable.function == FunctionInfo::name` (`KD/src/core/session.cpp:660-667`).
Probe: a C++ class method named `Counter::add` (symbol `Counter_add`) with
parameters `this`, `by` and local `tmp` translated to `FunctionInfo.name =
"Counter::add"` and `VariableInfo.function = "Counter_add"`; `session->locals(0)`
returned empty for that frame while `_start`'s local was listed. KR32's own
reader and GUI have the same name comparison (`KR32/src/debug/debug.cpp:737-742`,
`KR32/src/ui/debugger_ui.cpp:2384`, `KR32/src/dap/dap.cpp:797`), so this is
consistent across consumers. kr216 already writes the display name in `V`/`W`
(`KC/src/backend/kr216_backend.cpp:1846-1852,1870-1876`), which is the variant that
works with krdbg; kr16 follows KR32 (`KC/src/backend/kr16_backend.cpp:1215-1221`).
A kr232 rewrite should emit the same name in `F.name` and `V.function`.

Also note `frame_base_for(function)` in `members(VariableView)` looks a frame up by
function name (`KD/src/core/session.cpp:207-218`), so the same mismatch makes C++
member expansion read from frame base 0; the name contract fixes both.

### 6.2 Internal-linkage symbols with the same name collide

`attach_kdbg` keys linked symbols by bare name, first record wins
(`KC/src/linker/linker.cpp:758-761`), because the bridge's `F`/`G` labels are bare
symbol names. Probe: two translation units each define `static int helper(...)`;
the KDBG dump showed two `function helper` records with the *same* range
(`0x10000-0x10058`), and the second unit's line records — whose labels
(`.Ldbg.helper.<n>`) collide with the first unit's — also resolved to the first
helper's addresses, so both units' lines name one body. krdbg translated both
functions and `session->locals(0)` listed the first helper's `x`,`y` twice; the
second helper's real body lies outside every `F` range, so a stop there has no
function, and the KR32 adapter's unwind produces no frame at all
(`KD/adapters/kr32/kr32_adapter.cpp:81-96`), while `line_at` reports the first
unit's last line. The same
happens to two static globals named `counter`: both `G` records resolved to the
first object's address; krdbg lists the name once at that address. This is a
producer/linker limitation, not a krdbg one: labels for internal-linkage symbols
must be unique across translation units (the kr232 assembler already hashes the
file for its line labels, `KC/src/asm/kr232_assembler.cpp:527-546`; its
text-label pass still uses bare names, `:551-560`).

### 6.3 A KR32 image with no `.kdbg` cannot be loaded

`--dump-symbols` on an image assembled from `.s` failed with
`section '.kdbg' was not found`, and `krdbg_demo --headless --kr32` on it failed
with `KR32 load failed: section '.kdbg' was not found`
(`KD/adapters/kr32/kr32_adapter.cpp:493-496`). The payload must exist even if it
contains only the `Build` record.

### 6.4 The function range excludes the epilogue

KR32's `F` end is `.L<symbol>.ret`, before the epilogue
(`KC/src/backend/kr32_backend.cpp:1588-1609`), while `K` ends at `.L<symbol>.end`
after it (`:1511-1515,1609`). Probe: for a function the dump reported function
`0x10000-0x10094` and scope `0x10000-0x100a0`. A debugger reading KDBG therefore
has no function and no frames for a pc in the epilogue; a producer that wants the
epilogue covered must extend the `F` end label, not just the scope.

## 7. Minimum record set

### 7.1 `-O0` C

The minimum that gives krdbg line stepping, breakpoints, parameters/locals,
globals and call stacks:

1. `S` for every source file; `id` unique per payload after the linker merge.
2. `L` per statement/instruction, plus one at `.L<symbol>.begin` for the
   definition line.
3. `F` for every function: `<display-name> <symbol> .L<symbol>.ret <frame>
   <return-type>`, with every `L` address inside `[symbol, .L<symbol>.ret)`.
4. `V` for every parameter and named local: `<symbol> <name> <slot-offset>
   <type> <size>` (offset from `R7`, positive).
5. `W` for the same names: `param`/`local` kind, function-scope name, `0x0`.
6. `T` for every referenced type, with correct sizes (including every member,
   array element and pointer target type).
7. `C`+`M` for structs, `A` for arrays, `R` for pointers that appear.
8. `G` for every defined global, including internal-linkage ones, with a
   non-zero size and a unique resolvable label.
9. `K` optional (krdbg ignores it); `P` not needed at `-O0` (nothing is promoted
   there: `compute_allocation` returns early when not optimising,
   `KC/src/backend/kr32_backend.cpp:1629-1634`).
10. Encode as KDBG v1.1 with `pointer_width = 4` and `image_base =` the
    container's base for KR32; the adapter will not load a KR32 image without the
    section.

### 7.2 `-O0` C++

Everything in 7.1, plus:

- `F`'s display name and `V.function`/`W.function` must be the **same string**;
  use the display name (the kr216 variant) or krdbg shows no member-function
  locals (§6.1).
- `C` must write the base class name, and base types must be emitted (`T`) before
  the derived `C` if a reader is to follow them; krdbg resolves the name but
  ignores inheritance when expanding, so inherited fields will not be displayed
  even when emitted (`KD/src/kdbg/kdbg.cpp:97` vs
  `KD/src/core/variables.cpp:152-190`).
- `M` records for the class's own fields, with member type names that have `T`
  sizes.

### 7.3 Hand-written `.s`

There is no working KR32 path today. The KR32 assembler accepts a `.debug_info`
directive but discards its contents (`KC/src/asm/kr32_assembler.cpp:843-847`),
`krc -c prog.s` is rejected for KR32 because only KR16/KR216/KR232 accept assembly
input for `-c` (`KC/src/tools/krc/main.cpp:839-848`), and `krc image` assembles a
non-object input with an **empty** bridge (`:366-375`), so no `.debug_info` is
added and no `.kdbg` is attached; krdbg then refuses the image (§6.3). krdbg's
host chain likewise accepts `.s` only for kr16 and kr232
(`KD/src/host/build_chain.cpp:74-75,171-216`; every other target hands the source
to `krc -c` or `krld`, and KR32's assembler rejects `-c` on `.s`,
`KC/src/tools/krc/main.cpp:839-848`). Even if a producer added `L`-only
records, `attach_kdbg` would drop them because no `F` range covers them
(`KC/src/linker/linker.cpp:973-985`); the KR32 VM's own reader accepted line
records without function records (`KR32/src/debug/debug.cpp:638-652`), but the
KDBG path does not. A KR232 rewrite must synthesise the bridge from assembly text
— the precedent is the kr232 assembler, which emits synthetic line labels, an
`S`, `L`, `F` and `G` records and an end label
(`KC/src/asm/kr232_assembler.cpp:510-560`).

## 8. What this reference does not cover

- The KR32 VM's own reader (`KR32/src/debug/debug.cpp`) reads the `.debug_info`
  bridge directly and not KDBG; its behaviour is cited only for the `P` semantics
  in §5 and the assembly-line tolerance in §7.3. It was not executed.
- The surface's rendering of `VariableView`/`MemberView` was inferred from the
  session API it calls (`KD/src/ui/surface.cpp:1030-1072`), not exercised.
- kr232's current emission is audited separately (task-8); this page mentions it
  only where the contrast is needed.
- The historical KR32 `P` producer and the kr232 promotion design are covered by
  [register-promoted-locals.md](register-promoted-locals.md); §5 here records the
  format and the krdbg-side contract only.
- The KR216 adapter's handling of the kr216 backend's `frame_size - slot` offset
  convention was not checked (`KD/adapters/kr216/kr216_adapter.cpp:101-124` does
  not negate it); that belongs to the kr216 audit, not this reference.

## Evidence

Commands run to verify the statements above (KRCompiler
`../krcompiler/build/windows-msvc-release`, or the installed
`../build/family-prefix/bin/krc.exe`; krdbg build `KD/build/windows-msvc` for the
probes):

| # | What was run | Observed |
|---|---|---|
| 1 | `krc --target=kr32 -std=c++17 -O0 -c probe.cpp -o probe.kro` then `krc image ... probe.kro -o probe.kr32`, then `krc --dump-symbols --json probe.kr32` | `functions:[{name:"Counter::add",...}]`, `variables` with `function:"Counter_add"`, `locations:[]`, `variableInfo` kinds `param`/`local`, `flags:0` |
| 2 | same page with `-O1` | identical `variables` records, `locations:[]` |
| 3 | `krdbg` translation probe linking `KD/build/windows-msvc/krdbg_kdbg.lib` | `FunctionInfo.name = "Counter::add"`, `VariableInfo.function = "Counter_add"`, `flags 0x1/0x2`, location kind `FrameOffset` |
| 4 | session probe linking `krdbg_core.lib`, `FakeTarget` set to a function's `begin` | `session->function_at` names `Counter::add`, `locals(0)` empty; for `_start`, `locals(0)` lists `c` |
| 5 | two-TU probe, each unit `static int helper(...)` | `--dump-symbols` printed two `function helper` records with the same range `0x10000-0x10058`; translated image had four functions and `locals(0)` listed `x y x y` |
| 6 | two-TU probe, each unit `static int counter` | both `symbol counter` records at `0x100b4`; map showed the real symbols at `0x100b4` and `0x100b8`; translated globals listed `counter` once |
| 7 | `.s`-only image through `krc image` | `krc --dump-symbols`: `section '.kdbg' was not found`; `krdbg_demo --headless --kr32`: `KR32 load failed: section '.kdbg' was not found` |
| 8 | hand-built KDBG payloads through a krdbg probe (vendored encoder) | `S+L+F` only: `function_at="main"`, `line_at` line 4, one frame, zero locals/globals; `S+L` only: `line_at` line 7, `function_at` none |
| 9 | C++ translation probe also showed function/scope ranges | `function 'Counter::add' 0x10000-0x10094`, `scope Counter_add 0x10000-0x100a0` |

Related format documents: `KC/docs/symbols.md` (record and KDBG tag tables),
`KC/docs/decisions/0009-kdbg-v1-freeze.md`, `KC/docs/decisions/0052-kdbg-variable-scope-ranges.md`,
`KC/docs/plan-o1-debugger.md`, `KC/docs/optimize.md` (§7 rows S4/E4/E6, §10 F2),
`KR32/docs/debug.md` (the VM reader's `.debug_info` view).
