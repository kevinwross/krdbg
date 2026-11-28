# Backend debug symbols: the guide a new KR virtual machine follows

**Status:** reference and porting guide, 2026-11-27; updated after the kr232
producer (`923bca2`, `fcbf8f7`) and krdbg consumer (`e71fabf`, `883e328`) landed,
and after the kr216 producer parity work (`e2dd57c`). It describes the present
tree; it is not a plan and it changes no code. The kr232 and promotion work is
implemented; statements that depend on a particular producer policy name the
target explicitly, and the general sections do not assume kr232.

**Who this is for.** The engineer who has to give a KR virtual machine (kr216
first, then a new architecture) the debug symbols krdbg needs, without
rediscovering the bridge format, the linker's filtering rules, the promotion
policy, or the validation procedure. Read §1–§2 end to end; then §3 and §4 for
the two halves that are hardest to get right; keep §6 as a checklist of known
failure modes; run §9 before claiming the port is done.

**Path convention.** `KC/` is the KRCompiler checkout (`../krcompiler/`), `KD/`
is this repository, `KR32/` is the KR32 VM checkout (`../kr32/`). Every `file:line`
anchor is relative to the named checkout, at the revisions above. The working-tree
kr232 and krdbg changes shift some line numbers; the anchors name the HEAD
snapshot and the code the anchor points at is named in words so it can be found
after the change lands.

**Companion documents.** This guide states the durable contract and the porting
order. Four sibling documents go deeper on one axis each and are the place to
look when this guide is not enough:

| Document | What it owns |
|---|---|
| [debug-symbols-reference.md](debug-symbols-reference.md) | The record-by-record derivation from the KR32 path, with the probe evidence. |
| [register-promoted-locals.md](register-promoted-locals.md) | Promotion, the `P`/`Location` record, and the krdbg consumer (landing in the working tree). |
| [kr232-debug-symbols-gap.md](kr232-debug-symbols-gap.md) | The ranked current kr232 gap list (G1–G12) with wire-level evidence; §7 summarises it durably. |
| [symbol-verification-plan.md](symbol-verification-plan.md) | The fixture set, the exact session assertions, and the pass/fail matrix for kr232. |

---

## 1. Audience and contract

### 1.1 The pipeline, in one picture

A backend never writes KDBG. It writes a NUL-terminated text *bridge* into a
`.debug_info` section; the linker parses that bridge after layout and encodes
the binary `.kdbg` payload; krdbg decodes the payload into its neutral
`DebugInfo` model; the session answers questions from the model and from the
adapter's machine state.

```
KRCompiler backend ──text bridge──> `.debug_info` section in one .kro object
        (S/L/F/G/V/W/K/P/T/C/M/A/R)
                 │  merge_debug_records(): concatenate, dedupe S by path, remap L source ids
                 ▼
              krld: attach_kdbg(): resolve every label against the linked
                 │  symbol table; build KdbgImage; drop orphan L; encode KDBG v1.1
                 ▼
              container (`.kr32`/`.kr16`/`.kr216`/`.kr232`): `.kdbg` section
                 │  (KR32 also keeps the legacy `.debug_info` bridge for its own
                 │   reader; the other containers keep only `.kdbg`)
                 │  krdbg adapter: find/read payload (read_section or read_payload)
                 ▼
              KDBG → DebugInfo (KD/src/kdbg/kdbg.cpp), target id, pointer bits
                 │
                 ▼
              DebugSession: line_at, function_at, locals, globals, members,
              call_stack, add_line_breakpoint — plus IDebugTarget::unwind()
```

Producer side: `KC/src/toolchain/toolchain.cpp:169-181` appends the bridge for
the direct-bridge backends; `KC/src/linker/linker.cpp:123-196`
(`merge_debug_records`) merges objects; `KC/src/linker/linker.cpp:742-1034`
(`attach_kdbg`) resolves labels and encodes. Consumer side:
`KD/src/kdbg/kdbg.cpp:62-218` translates, `KD/src/core/session.cpp` answers the
session API, and each adapter supplies `unwind()` and the frame base.

### 1.2 What krdbg needs, and what it will tolerate

krdbg is deliberately forgiving: a payload with only `S`+`L`+`F` still decodes
and gives line lookups and a call stack. But "decodes" is not "debugs". The
minimum that makes each feature correct:

| Feature | Minimum the producer must supply | Why / where consumed |
|---|---|---|
| Source list | `S` for every file an `L` names (or at least the TU file) | `find_source` matches path → basename → suffix (`KD/src/core/debug_info.cpp:230-255`); line breakpoints look the file up first and give up if absent (`KD/src/core/session.cpp:125-141`) |
| Line stepping and breakpoints | `L` for every instruction that has a source location, plus one at the function entry label | `line_at` takes the last record at or before the pc, clamped to the enclosing function (`KD/src/core/debug_info.cpp:175-228`); line-over compares `LineInfo::function` (`KD/src/core/session.cpp:484-519`) |
| Function bounds | `F` covering every `L` address; `F.start` == the definition-line `L` address | the linker drops lines outside every function (`KC/src/linker/linker.cpp:973-985`); `function_at` drives stepping, locals and frame names (`KD/src/core/debug_info.cpp:154-173`) |
| Parameters and locals | `V` (function, name, frame offset, type, size) + `W` (kind) + `T` (type), with `V.function` equal to the function's identity (the `F` display name, or a symbol bridged by a function-scope `K`) | `locals()` requires the pc's function and a matching identity and not global (`KD/src/core/session.cpp:645-669`; working tree uses `FunctionInfo::identity()`); `W` decides param/local (`KD/src/kdbg/kdbg.cpp:170-185`); a missing `T` shows `?` and formats as a signed integer (`KD/src/core/variables.cpp:93-150`) |
| Globals | `G` with a resolvable label, non-zero size, and a `T` for the type; include internal linkage; unique labels per object | `G` becomes an absolute `VariableInfo` (`KD/src/kdbg/kdbg.cpp:189-217`); `globals()` lists it (`KD/src/core/session.cpp:671-685`); `size == 0` is ignored (`:192-194`) |
| Aggregates | `T` sizes for every member/element/pointee type, `M` offsets, `A` element+count, `R` pointer target, `C` base | member expansion slices bytes by member offset and member type size (`KD/src/core/variables.cpp:152-190`); arrays need `element`+`count` (`KD/src/core/session.cpp:720-753`) |
| Call stack | `F` ranges that cover every return address, with the name each frame should show | the adapter walks frames; the session labels each with `function_at(pc)` (`KD/src/core/session.cpp:626-643`); the KR32 walker stops when `find_function` fails (`KD/adapters/kr32/kr32_adapter.cpp:76-98`) |
| Unwind | `F.frame` only if the adapter uses it (KR32 does); otherwise the adapter's own chain is enough | `FunctionInfo::frame` is the ABI offset from the frame pointer to the saved caller frame (`KD/include/krdbg/debug_info.h:51-61`) |
| Scopes (`K`) | Optional to krdbg's display, but it is the record that lets a reader recover a function's symbol when the display name differs from the name the variable records use (§6.1) | `K` is decoded into `KdbgImage.scopes`; the working-tree translator reads the function-scope `K` to derive `FunctionInfo::symbol` and `identity()` (`KD/src/kdbg/kdbg.cpp`, `KD/include/krdbg/debug_info.h`) |
| Register locations (`P`) | Optional until a backend keeps a named variable in a register; no producer exists | decoded into `KdbgImage.locations`; the working-tree translator turns each into a `LocationInfo` range and the session resolves it at the frame's pc (HEAD `71e4591` dropped them; `KD/src/kdbg/kdbg.cpp:62-218` at HEAD never reads `image.locations`) |

"Tolerated" means degraded, not equivalent. Missing `F` loses function
association and the call stack; missing `S` makes line breakpoints never
resolve; missing `W` lists every variable as a local; a `V` with no matching
`T` reads as an integer. Do not design to the tolerated subset.

### 1.3 The minimum record set, by source kind

- **C at `-O0`:** `S` per file; `L` per statement plus a definition-line `L`;
  `F` per function; `V`+`W` per parameter and named local; `T/C/M/A/R` for every
  referenced type; `G` per defined global (internal linkage included).
- **C++ at `-O0`:** the C set, plus: the *same name string* in `F`, `V`, `W`
  (and `K`/`P` if emitted) so member-function locals are visible — use the
  display name (`Class::method`) in all of them, the kr216 variant; `C` with the
  base name; `M` for the class's own fields; a `V`/`W` for `this`.
- **Hand-written `.s`:** `S` for the `.s` file; `L` per physical source line;
  `F` per function with real bounds; `G` per named data object; `V`/`W` only if
  the assembly carries directives that define them. Physical line numbers are
  *correct* here — unlike generated assembly (§3.3).

### 1.4 Producer contract in ten rules

1. Addresses in the payload are **absolute in the image's load space**; the
   bridge itself carries labels, never addresses. `image_base` records the load
   base so a reader can reject a payload built for another base
   (`KC/docs/symbols.md:38-46`).
2. Ranges are **half-open**: `[start, end)`. A pc equal to `end` belongs to no
   record. Nothing slides: a variable/line/function with no record is reported
   as unavailable, not approximated from a neighbour.
3. One function name string per function, used identically in `F.name`,
   `V.function`, `W.function`, `K.function`, `P.function` (§6.1).
4. `F.start` is the function's entry byte and carries a line record for the
   definition line (ADR 0061, `KC/docs/decisions/0061-function-definition-location.md:31-57`).
5. Labels resolve by **bare name** against the linked symbol table, first
   definition wins; internal-linkage labels must therefore be unique across
   translation units (§6.2).
6. `V` is a promise for the *whole* function: named variables keep one home for
   the function's extent unless a `P` range says otherwise (§4).
7. Records are deterministic: fixed order, no timestamps, no host paths in the
   payload, source paths in one spelling (§3.6, §9.5).
8. Prefer a missing record to a wrong one: a line whose instruction a pass
   removed gets no `L`, not one slid onto a neighbour; a breakpoint that cannot
   be placed beats one that stops in the wrong place
   (`KC/docs/symbols.md:181-193`).
9. Emit no field whose meaning a reader would have to guess. `T` before
   `C`/`A`/`R`/`M`, `W` alongside `V`, `P` only with a `V` and a true range.
10. Do not write KDBG from a backend. Extend the bridge only with the format
    owner's agreement (`KC/docs/symbols.md`, ADR 0009); the linker owns KDBG.

---

## 2. The bridge records, one by one

### 2.0 Grammar, ordering, and labels

- The bridge is a byte stream of NUL-terminated records. `attach_kdbg` reads
  one record per NUL and tokenises it with `std::istringstream >>`, so every
  field is whitespace-delimited and a token cannot contain a space
  (`KC/src/linker/linker.cpp:800-814`). A path with a space cannot round-trip;
  use space-free paths.
- Within one object, emit `S` **before** any `L` that names a source id the
  `S` introduced. `merge_debug_records` builds the object's `S` remap while
  scanning in order and rewrites `L` ids from the map it has built so far
  (`KC/src/linker/linker.cpp:123-196`). Across objects order does not matter for
  the linker, but keep a fixed order for determinism.
- The linker keys **sources by path**: duplicate paths merge into one id and
  every object's `L` ids are remapped to it (`:147-189`). Per-TU ids from 0 are
  fine.
- The linker keys **labels by bare name** through the linked symbol map
  (`:758-761`), which contains local symbols too (`:542-563`). Records whose
  label does not resolve are silently dropped: an `L`, `F`, `G`, `P` or `K`
  with a mistyped label is not an error, it is an absence.
- `attach_kdbg` writes one KDBG payload: `target_id` from the KRO target id,
  `pointer_width` from the target, `image_base` from the linked base
  (`:763-782`), then the v1 sections and the v1.1 sections. The shipped
  container keeps `.kdbg` and drops the bridge for KR16/KR216/KR232
  (`KC/src/toolchain/toolchain.cpp:417-429`, `:438-449`, `:458-468`); KR32 keeps
  the bridge as well because its own reader still consumes it. KDBG is the
  contract either way.

Label conventions used by the complete producers — re-use them so a reader (and
a human looking at `-S` output) can recognise them:

| Label | Meaning |
|---|---|
| `<symbol>:` | the function symbol, the `F` start label |
| `.L<symbol>.begin` | same address as the symbol, before the prologue; carries the definition line |
| `.L<symbol>.ret` | before the epilogue; the KR32/KR16/KR216 `F` end label |
| `.L<symbol>.end` | after the epilogue; KR32's `K` end label |
| `.Ldbg.<symbol>.<n>` | one immediately before each instruction that has a source location |
| `.Lf<n>_bb<m>` etc. | backend-generated block labels; they must stay out of `F` start/end fields |

### 2.1 `S` — source file

Bridge: `S <id> <path>`. KDBG: `Source` tag 1 in `.kdbg_src`. krdbg:
`SourceFile{id, path, name=basename}` (`KD/src/kdbg/kdbg.cpp:73-80`).

- `id` is a `u32`, unique per payload after the merge. Emit one `S` per distinct
  `ir::DebugLocation::file` the TU references, numbered from 0.
- `path` is one token. Record the path the build was invoked with, relative to
  a root, with `'/'` separators; `krld --source-root=DIR` rewrites paths under
  `DIR` to root-relative paths and leaves others alone
  (`KC/src/linker/linker.cpp:818-828`, `KC/docs/symbols.md:200-205`). krdbg
  resolves relative paths against the image directory and then the configured
  source roots (`KD/adapters/kr232/kr232_adapter.cpp:590-619`).
- Emit `S` only for files an `L` actually names; an `S` with no matching `L` is
  harmless but useless, and a file named by `L` with no `S` is a line record
  with an id no reader can resolve.

```cpp
void add_debug_line(const std::string& label, const ir::DebugLocation& loc) {
    auto [it, inserted] = sources_.try_emplace(loc.file, next_source_);
    const std::string id = std::to_string(it->second);
    if (inserted) {
        ++next_source_;
        bridge_ += "S " + id + " " + loc.file + '\0';
    }
    bridge_ += "L " + label + " " + id + " " + std::to_string(loc.line) + " " +
               std::to_string(loc.column) + '\0';
}
```

### 2.2 `L` — source line

Bridge: `L <label> <source-id> <line> <column>`. KDBG: `Line` tag 2 (or
`Line64` tag 15 when pointer width is 8). krdbg keeps address, source and line;
column is dropped (`KD/src/kdbg/kdbg.cpp:126-138`), but the token must be
present — write `0` if the front end has no column.

Two kinds of record are required:

1. **Definition line:** immediately after `<symbol>:` and `.L<symbol>.begin`,
   emit `L <begin-label> <id> <definition.line> <col>` when the definition
   location is valid. The label and the symbol share an address, so `F.start`
   and the definition-line record agree and a breakpoint on `{` binds before
   the prologue (ADR 0061; `KC/src/backend/kr32_backend.cpp:1307-1315`).
2. **Instruction lines:** a label immediately before each instruction that
   carries a source location, and an `L` for that label
   (`KC/src/backend/kr32_backend.cpp:1488-1494`).

Rules and failure modes:

- The linker resolves the label to an address and drops the record when the
  label is unknown, or when the address falls outside every `F` range
  (`KC/src/linker/linker.cpp:830-838`, `:973-985`). A line label that shares an
  address with the function end is dropped — this is how a folded-away `break;`
  disappears.
- `line` must be a real line in the file named by the `S` record. This is the
  single most common way to ship a debugger that "works" while showing nonsense
  (§3.3).
- Do not map a line whose instruction a pass removed onto a neighbouring line:
  the format's policy is no record (`KC/docs/symbols.md:181-193`).
- A pc in a function's epilogue has no line and, when the `F` end precedes the
  epilogue, no function either (§5.2).

### 2.3 `F` — function

Bridge:
`F <display-name> <symbol/start-label> <end-label> <frame-size> <return-type>`.
KDBG: `Function` tag 3 (or `Function64` tag 16). krdbg keeps name, range and
frame; it ignores the return type and fills `FunctionInfo::source/line` with
zero (`KD/src/kdbg/kdbg.cpp:110-124`).

- `display-name` is what the Variables window and call stack show;
  `Class::method` for a member function, the symbol otherwise
  (ADR 0030, `KC/docs/decisions/0030-debug-display-names.md`). `symbol` is the
  function symbol and doubles as the start label.
- `end-label` is the half-open end. Choose deliberately:
  - KR32/KR16/KR216 end at `.L<symbol>.ret`, **before** the epilogue. A pc in
    the epilogue has no function; the stack cannot name that frame.
  - KR232's synthesised `F` ends at the next function label or the end of
    `.text`, so the epilogue is covered.
  - Both are defensible; write down which one the backend chose. What is not
    defensible is an end label that does not exist, or a range that stops before
    a call site's return address.
- `frame-size` is the ABI number the unwinder adds to the current frame pointer
  to reach the saved caller frame: saved FP at `FP + frame`, return PC at
  `FP + frame + pointer_size` (`KC/src/backend/kr32_backend.cpp:1588-1609`;
  `KD/include/krdbg/debug_info.h:51-61`). It is **not** the variable-frame
  origin. A fixed-layout ABI that unwinds from `[FP]`/`[FP+4]` can write `0`
  and ignore it (KR232 does).
- `return-type` is a positional token even when ignored; write the normalised
  type spelling, or `0`/`void`.

```cpp
const auto display = fn.debug_name.empty() ? fn.name : fn.debug_name;
bridge_ += "F " + display + " " + fn.name + " .L" + fn.name + ".ret " +
           std::to_string(layout.frame_size) + " " + debug_type_name(return_type) + '\0';
```

### 2.4 `G` — global data symbol

Bridge: `G <name> <label> <type> <size>`. KDBG: `Symbol` tag 4 (or `Symbol64`
tag 17). krdbg exposes every `G` symbol with non-zero size, not matching a
function name and not already declared as a function-less variable, as an
**absolute global** variable (`KD/src/kdbg/kdbg.cpp:189-217`).

- Emit for every **defined** module global: external definitions and
  internal-linkage definitions. Skip external *declarations*
  (`global->external`) — they have no definition here
  (`KC/src/backend/kr32_backend.cpp:1062-1081`).
- `label` is resolved against the linked symbol table; the name is what the
  Variables window shows. Usually `name == label`, but a name does not have to
  carry a resolvable label.
- `size` must be the real byte size; zero is ignored by krdbg.
- `type` must have a `T` record with the same size, or the value formats as a
  signed integer.
- **Labels must be unique across translation units.** Two `static int counter;`
  in two objects resolve to the same bare name; the first symbol wins
  (`KC/src/linker/linker.cpp:758-761`), so both `G` records point at the first
  object's address (§6.2).

```cpp
for (const auto& g : module_.globals()) {
    if (g->external) continue;
    add_debug_type(g->element_type);
    bridge_ += "G " + g->name + " " + g->name + " " +
               debug_type_name(g->element_type) + " " + std::to_string(size) + '\0';
}
```

### 2.5 `V` — variable frame home

Bridge: `V <function> <name> <frame-offset> <type> <size>`. KDBG: `Variable`
tag 5 in `.kdbg_var`. krdbg sets `function`, `name`, `type`, `size`, and a
`LocationKind::FrameOffset`; it interprets the payload's `u32` offset as a
**signed 32-bit** displacement and adds it to the frame base
(`KD/src/kdbg/kdbg.cpp:156-169`, `KD/src/core/variables.cpp:78-83`).

- `function` must be the **same string** as `FunctionInfo::name` (the `F` display
  name). `locals()` compares them literally (`KD/src/core/session.cpp:660-667`).
- `name` is the source name: the parameter name, or the local's IR name with the
  `.addr` suffix stripped.
- `frame-offset` is a displacement from `RawFrame::frame_base`, whose value the
  adapter chooses. Two conventions exist in the tree; pick one and test it on
  both directions of access (read and member expansion):
  - **positive from the frame base** — KR32: bridge writes the slot offset from
    `R7`, the adapter reports `frame_base = R7`, the value is at
    `R7 + offset`.
  - **negative from the frame base** — KR232: bridge writes `-offset`, the
    adapter reports `frame_base = EFP`, the value is at `EFP - offset`.
  - **positive from the opposite end, negated by the adapter** — KR16/KR216:
    bridge writes `frame_size - slot`, the adapter negates it at load so the
    session's addition still lands below the frame base
    (`KD/adapters/kr16/kr16_adapter.cpp:680-685`,
    `KD/adapters/kr216/kr216_adapter.cpp:371-376`).
  Negative bridge offsets survive the round trip only because the field is a
  `u32` holding two's complement and krdbg reinterprets it as `int32`
  (`KC/include/krc/debug/kdbg.h:85-91`); a different KDBG consumer may read it
  unsigned, so prefer the positive-from-frame-base form for new work and put
  the negation in the adapter deliberately.
- `size` is the object's byte size. A zero size and a frame location makes the
  read fail (`KD/src/core/variables.cpp:47-50`).

### 2.6 `W` — variable kind, scope, flags

Bridge: `W <function> <name> <kind> <scope> <flags>`. KDBG: `VarInfo` tag 11 in
`.kdbg_var`. The linker accepts `param`, `local`, `static` and drops any other
kind; it parses `flags` with `strtoul(..., 0)`
(`KC/src/linker/linker.cpp:921-937`).

- krdbg maps `param`→`VariableParam`, `local`→`VariableLocal`,
  `static`→`VariableStatic`; it copies `scope` but reads no flag bit
  (`KD/src/kdbg/kdbg.cpp:170-185`). Without `W` every `V` is a local.
- `scope` is a `K` name, or `-` for the function scope. KR32 writes the function
  symbol.
- `flags` is a `0x`-prefixed `u32`: bit 0 address-taken, bit 1 read-only, all
  others reserved 0 in `KC/docs/symbols.md:101`. Producers currently write `0x0`.
  Bit 2 ("home slot unusable") is the proposed way to say a named variable no
  longer has a valid home; the krdbg working tree consumes it as
  `VariableSlotUnusable` and reports `<optimized out>` outside every `P` range.
  A producer that writes it must also update `docs/symbols.md`: the frozen format
  still calls bit 2 reserved.
- Emit `W` for exactly the names `V` emitted, with the same function string.

### 2.7 `K` — lexical scope

Bridge: `K <function> <name> <parent> <start-label> <end-label>`. KDBG: `Scope`
tag 10 in `.kdbg_scope`. `K` is optional for display, but it is the record that
bridges a display name to the symbol the variable records use: the working-tree
translator reads the function-scope `K` (`parent` empty, `start == F.start`) to
set `FunctionInfo::symbol` when the payload's `V`/`W` name the function by it
(§6.1; `KD/src/kdbg/kdbg.cpp`). At HEAD the record is decoded and ignored. If
emitted: `parent` is another scope name or `-`; range is half-open and may be
wider than the `F` range (KR32's `K` ends after the epilogue, at
`.L<symbol>.end`); both labels must resolve or the linker drops the record
(`KC/src/linker/linker.cpp:904-920`).

### 2.8 `P` — register location over a live range

Bridge: `P <function> <name> <reg> <start-label> <end-label>`, where `reg` is
`R0`–`R15` or `F0`–`F15` (an `F` prefix sets the fp flag). KDBG: `Location`
tag 9 in `.kdbg_loc` (or `Location64` tag 18). Range half-open, several records
per variable allowed. The consumer is landing: at HEAD `71e4591` `translate()`
never reads `image.locations` (`KD/src/kdbg/kdbg.cpp:62-218`); the working tree
2026-11-27 adds `LocationInfo{function, name, Location, start, end}` to
`DebugInfo`, sorts ranges by `(function, name, start)`, resolves the range that
contains the frame's pc, and reports a register value in a caller frame as
unavailable rather than reading the current register
(`KD/include/krdbg/debug_info.h`, `KD/src/core/session.cpp`;
`KD/src/core/variables.cpp:52-77` is the register read path). A producer still
does not exist.

The contract if a backend ever produces one:

- It accompanies a `V` (type, size) and a `W` (kind, scope). A `P` alone carries
  no type and no size.
- The register must actually hold the variable over the whole interval. No krc
  backend can promise that today: the KR32 allocator keeps only single-block
  temporaries, and the kr232 emitter materialises operands into scratch
  registers per instruction ([register-promoted-locals.md](register-promoted-locals.md) §1.1, §2.1, §3.1).
- Prefer a whole-function range anchored on the function symbol and `.ret`/`.end`
  labels when the value is register-resident for the entire body; the range then
  includes the stretch before the first assignment, and an unassigned variable
  reads the register's stale contents rather than "unavailable". That is the
  accepted KR32 contract (`KR32/docs/debug.md:40-51`).
- Never insert labels during the pipeline to bracket a range; anchor on labels
  that already exist and append the `P` text after all passes. Label churn is
  how KR32's `P` records went out of range (`KR32/implementation.md:4264-4272`).
- The same function-name string as `F`/`V`/`W`; the C++ label/name mismatch is
  what made KR32's C++ `P` records wrong.

Work order if you need promoted variables visible: the krdbg consumer is in the
working tree (`LocationInfo`, range selection, `<optimized out>` via
`VariableSlotUnusable`, caller-frame unavailability); what is missing is the
producer and its tests. A producer with no consumer is invisible; a consumer
with no producer is a test fixture. Do not promote a named variable before both
exist and a fixture proves the value at both ends of the range (ADR 0052).

### 2.9 `T`, `C`, `M`, `A`, `R` — types

Bridge payloads:

| Record | Payload | Meaning |
|---|---|---|
| `T` | `<name> <size>` | a named type and its byte size |
| `C` | `<name> <size> <base-or-minus>` | a struct and its base class name (`-` for none) |
| `M` | `<type> <field> <offset> <field-type> <field-size>` | one struct field |
| `A` | `<type> <element-type> <count>` | an array type |
| `R` | `<type> <target-type>` | a pointer type and its target |

KDBG: `Type` tag 6, `Member` tag 7, plus `Base` 12, `ArrayType` 13, `TypeRef`
14 for the v1.1 facts. krdbg builds `TypeInfo{name,size,kind,members}` and
resolves `base`, `element`, `element_count` and member types by name
(`KD/src/kdbg/kdbg.cpp:82-108`).

- Type names are one token: strip the IR `%` sigils and replace spaces with
  `_`. Scalars become `i8`/`i32`/`i64`/`f32`/`f64`; arrays `[4_x_i32]`;
  pointers `ptr<i32>`; structs their tag. The exact spelling is not important
  as long as `V`/`G`/`M`/`A`/`R` use the same spelling and a `T` with the right
  size exists.
- Emit the dependency first: a struct's base, field and member types before the
  `C`/`M` records that reference them. KR32's `add_debug_type` is depth-first and
  idempotent through a `std::unordered_set<std::string>` of emitted names
  (`KC/src/backend/kr32_backend.cpp:809-876`).
- `M.offset` is the field's byte offset from the object's start; `M.field-size`
  is stored in KDBG but krdbg ignores it and uses the field type's `T` size, so a
  missing/mismatched `T` for a member prints `?`.
- Unions: KR32 emits only `T` for a union (the kind chain has no union branch,
  `KC/src/backend/kr32_backend.cpp:831-875`), so it reaches krdbg as kind
  `builtin` and does not expand; KR232 emits `C` for unions too
  (`KC/src/backend/kr232_backend.cpp:1394-1400`), and KDBG has no union tag, so
  the reader sees kind `struct` with flattened member offsets
  ([kr232-debug-symbols-gap.md](kr232-debug-symbols-gap.md) "Shared with KR32").
  If a language needs true union semantics, add the kind to `docs/symbols.md` and
  to the KDBG reader, not just to the producer.
- Enums have no bridge record; `TypeKind::Enum` exists but nothing produces it.
- Types are emitted per TU; the linker creates each type on its first `T` and
  overwrites the kind on `C`/`A`/`R`, de-duplicating members by name
  (`KC/src/linker/linker.cpp:784-798`, `:938-953`, `:954-970`). Emit them
  deterministically anyway.

### 2.10 Architecture notes

- **Pointer width.** KDBG's header byte at offset 24 selects the record set:
  width 8 uses `Line64`/`Function64`/`Symbol64`/`Location64`; a 32-bit target
  writes only the v1-wide records. The linker derives it from the target id —
  `2` (KR16) and `4` (KR216) → 2 bytes, everything else → 4
  (`KC/src/linker/linker.cpp:766-769`). A new 64-bit target needs that branch
  widened **and** the KDBG encoder/reader exercised at width 8 before the
  addresses are trusted; `attach_kdbg` currently narrows every resolved symbol
  value to `u32` (`:758-760`), so a 64-bit target must widen that first.
  krdbg turns `pointer_width` into `DebugInfo::pointer_bits`
  (`KD/src/kdbg/kdbg.cpp:66-69`).
- **Endianness.** The KDBG payload is little-endian by construction (header byte
  25 is 1). krdbg formats values with the adapter's
  `TargetLayout::little_endian` flag, so a big-endian VM needs the adapter flag
  and a KDBG endianness decision, not just an emitter change.
- **Register files.** `P`'s register number is target-specific. krdbg's neutral
  convention is the adapter's group ids: `gpr` for non-fp, `fpr` for fp
  (`KD/adapters/kr232/kr232_adapter.cpp:408-446` has `gpr` 0–15 and `fpr`
  0–3). The translator, not the UI, maps `fp` to the group; keep VM types out
  of `src/ui`.
- **Address space.** krdbg's model is 64-bit addresses regardless of target.
  Keep every address in the payload absolute in the image's load space; the
  `image_base` header field exists so a reader can refuse a mismatched payload
  (`KC/docs/symbols.md:38-46`; the linker's KR232 special case is at
  `KC/src/linker/linker.cpp:775-782` — replicate that pattern if a VM's link
  base differs from its container base).
- **Frame and unwind.** See §5. `F.frame` is an unwinder input, not a variable
  base; `RawFrame{pc, frame_base}` is the adapter's output
  (`KD/include/krdbg/target.h:97-100`); the adapter, not KDBG, decides how the
  chain is walked.

### 2.11 Record emitter recipes at a glance

Once the concepts above are in place, this table is the whole emitter. "Type
token" means the normalised type name (§2.9); every token is whitespace-free.

| Record | Emit once per | Pseudocode |
|---|---|---|
| `S` | distinct source file | `bridge += "S " + id + " " + path + '\0'` |
| `L` | instruction with a source location, plus the entry label | `bridge += "L " + label + " " + source_id + " " + line + " " + column + '\0'` |
| `F` | defined function | `bridge += "F " + display_name + " " + symbol + " " + end_label + " " + frame + " " + ret_type + '\0'` |
| `G` | defined global | `bridge += "G " + name + " " + label + " " + type_token + " " + size + '\0'` |
| `V` | parameter, or `<name>.addr` local | `bridge += "V " + function + " " + name + " " + offset + " " + type_token + " " + size + '\0'` |
| `W` | same names as `V` | `bridge += "W " + function + " " + name + " " + kind + " " + scope + " 0x0\0"` |
| `K` | optional function scope | `bridge += "K " + function + " " + function + " - " + begin_label + " " + end_label + '\0'` |
| `P` | register-resident interval (future) | `bridge += "P " + function + " " + name + " " + reg + " " + start_label + " " + end_label + '\0'` |
| `T` | named type | `bridge += "T " + type_token + " " + size + '\0'` |
| `C` | struct | `bridge += "C " + type_token + " " + size + " " + base_or_minus + '\0'` |
| `M` | struct field | `bridge += "M " + type_token + " " + field + " " + offset + " " + field_type + " " + field_size + '\0'` |
| `A` | array type | `bridge += "A " + type_token + " " + element_type + " " + count + '\0'` |
| `R` | pointer type | `bridge += "R " + type_token + " " + target_type + '\0'` |

### 2.12 Emitter skeleton (direct-bridge style)

The order below matches the complete producers and keeps every record
deterministic; only the type collection and global pass are order-free for the
linker.

```cpp
std::optional<std::string> emit(std::string& error) const {
    reset_debug_state();                     // bridge_, sources_, types_, next_source_
    collect_types(module_);                  // T/C/M/A/R for everything referenced
    std::string out = assembly_header();

    for (const auto& global : module_.globals())   emit_global(global, out);
    for (const auto& fn : module_.functions()) {
        if (fn->declaration) continue;
        emit_function(out, *fn, layout_function(*fn));
    }
    return out;
}

void emit_function(std::string& out, const ir::Function& fn, const FrameLayout& layout) {
    out += linkage_directive(fn);            // .global / .weak for external/weak
    out += fn.name + ":\n";
    out += ".L" + fn.name + ".begin:\n";     // before the prologue
    if (fn.definition.valid()) add_debug_line(begin_label(fn), fn.definition);
    emit_prologue(out, fn, layout);          // push FP, frame adjust, save regs
    emit_parameter_homes(out, fn, layout);   // stores + V/W per parameter
    for (block : fn.blocks) {
        out += block_label(fn, block) + ":\n";
        for (instruction : block.instructions) {
            if (instruction.debug.valid()) {
                const auto label = ".Ldbg." + fn.name + "." + std::to_string(n++);
                out += label + ":\n";
                add_debug_line(label, instruction.debug);
            }
            emit_instruction(out, instruction);
        }
    }
    out += ".L" + fn.name + ".ret:\n";
    emit_epilogue(out, fn, layout);
    out += ".L" + fn.name + ".end:\n";
    emit_debug_function_record(fn, layout);  // F + K
    emit_local_homes(fn, layout);            // V/W per named .addr local, filtered
}
```

The text-assembly style (§3.2) splits this differently: the backend writes
directives, and an assembler builds the bridge records.

---

## 3. Source mapping: two backend styles

Both styles end in the same `.debug_info` bridge; they differ in who knows the
final label addresses and who sees the source location.

### 3.1 Style A — the backend writes the bridge directly

KR32, KR16 and KR216 hold a `debug_info_` string, append records as they emit
text, and return it through `toolchain::compile`; `toolchain::assemble` appends
it as the object's `.debug_info` section
(`KC/src/toolchain/toolchain.cpp:169-181`; KR32 at `:298-309`, KR16 at
`:258-269`, KR216 at `:270-281`; the backends' `emit` at
`KC/src/backend/kr32_backend.cpp:979-1105`, `kr16_backend.cpp:1272-1392`,
`kr216_backend.cpp:1909+`).

Why it works: the backend sees every IR instruction's `DebugLocation`, so it
can emit a label plus an `L` at exactly the instruction it is about to write,
including the definition line before the prologue. The linker resolves the
labels after layout; the emitter never needs an address.

Use this style when the backend emits instructions directly and controls the
assembly text. Its cost is that the debug text is emitted during code
generation, so an optimisation pass that runs *after* emission (a text peephole)
can delete an instruction whose label and line are already recorded. The linker
then drops the orphan line, which is correct, but only because it range-filters
(`KC/src/linker/linker.cpp:973-985`).

### 3.2 Style B — text assembly plus debug directives

KR232's backend emits text assembly only, with directives that carry debug
facts but no labels:

| Directive | Becomes | Emitted at |
|---|---|---|
| `.debug_type T\|C\|M\|A\|R ...` | the matching type records | before all code, `KC/src/backend/kr232_backend.cpp:1384-1424,1462` |
| `.debug_global <name> <type> <size>` | `G <name> <name> <type> <size>` | at each global's data label, `:1508-1513` |
| `.debug_var <function> <name> <offset> <type> <size>` | `V <function> <name> <offset> <type> <size>` | after the prologue, from each named alloca, `:1763-1788` |
| `.debug_line <line>` | the source line of every instruction until the next directive | before the body and when the IR debug line changes, `:1975-1986` |

The kr232 assembler parses those directives and **synthesises** the rest of the
bridge from the assembly text it has just encoded
(`KC/src/asm/kr232_assembler.cpp:510-592`):

- `S 1 <recorded source name>` — one source; the driver passes the original
  input path as `options.source_name` (`KC/src/toolchain/toolchain.cpp:282-290`),
  so a C/C++ build records the `.c`/`.cpp` path and a hand-written `.s` records
  the `.s` path.
- `L <synthetic-label> 1 <line> 0` for **every encoded instruction**. The
  assembler appends `DebugLine{offset, line, file}` for each instruction and
  branch (`:276-292`, `:355-364`, `:411-412`), then emits a unique local label
  at each offset and an `L` for it (`:542-547`).
- `F <name> <name> <next-text-label-or-end> 0 0` for every **global** text label
  (`:548-570`).
- `G` for every global data label (`:571-585`).
- `V` passed through from `.debug_var` (`:586-588`).

`toolchain::assemble` does **not** append the option's bridge for kr232, because
the assembler already produced one (`KC/src/toolchain/toolchain.cpp:295-297`).

Use this style when the backend's output is assembly that the assembler already
parses, or when the same assembler must also accept hand-written `.s` and give
it symbols. Its cost is the whole of §3.3: the assembler only knows the line
number the directive named, not the file, not the definition line unless a
directive says so, and not which labels are functions except by convention.

### 3.3 The masquerade failure mode: generated-assembly lines as source lines

If an instruction is encoded when no source line directive has set a line, the
kr232 assembler falls back to `line_number` — the physical line of the
**generated assembly text** (`KC/src/asm/kr232_assembler.cpp:276-292`,
`:355-364`, `:411-412`). For a hand-written `.s` that fallback is exactly right:
the assembly *is* the source. For compiler-generated assembly it is a lie: the
record names, say, line 1803 of a file with 25 lines.

Measured on checked-in fixtures before the `.debug_line` work: a 10-line C
source carried 16 line records spanning 7–29; `aggregate.c` (25 lines) carried
107 records spanning 27–141; `startrekng`'s `main_script.cpp` (8 lines) carried
records up to 240, and `console.cpp` (137 lines) up to 1861
([symbol-verification-plan.md](symbol-verification-plan.md) §3). The scenario
tests passed because they never compared a recorded line with the file. With
the `.debug_line` directive the same class of defect shrinks but does not
vanish: a rebuilt startrekng image is 55/32,307 out-of-range line records
(0.17%) against 95% in the installed-prefix build, because directive state can
still leak across function boundaries
([kr232-debug-symbols-gap.md](kr232-debug-symbols-gap.md) G1).

The failure has three layers:

1. **No mapping at all.** Without a source-line directive, every instruction gets
   a generated-assembly number. Fix: emit a line directive before each
   instruction that has a source location; never let generated code fall through
   to the assembly-line fallback.
2. **A mapping that starts too late.** A directive emitted only before the first
   body instruction leaves the prologue (`enter`/`push`/parameter spills) on
   generated numbers. Those numbers are inside the function's `F` range, so the
   linker keeps them; `line_at(function entry)` then reports a line that does not
   exist. Fix: emit the definition line's directive **immediately after the
   function entry label and before the first prologue instruction**, and either
   map every prologue instruction to a real source line or attach no line record
   to it.
3. **A mapping that leaks across functions.** The assembler's current line is
   state and does not reset at a function boundary. With a late directive, a
   function's prologue inherits the *previous* function's last line, which is a
   real line of a real file and therefore passes a line-range check while
   stepping from the entry jumps backwards into unrelated source. Fix: reset the
   state at every function entry — and because synthetic functions have no source
   location, the directive needs an explicit "no source line" form (the kr232
   assembler's `debug_line == 0` fallback must become a tri-state: no directive
   seen → assembly line is the source, as for hand-written `.s`; explicit zero →
   emit no `L` until the next directive; N → line N). The gap document's G1
   states the same requirement (`kr232-debug-symbols-gap.md:179-204`).

The checks are mechanical: every emitted line must satisfy
`1 <= line <= number_of_lines_in_its_file`, every function's start address must
carry its definition line, and the first recorded line of a function must not be
the previous function's last line (§9.1).

Treat this failure as load-bearing for any text-assembly backend: a suite
anchored on stop addresses passes while the debugger shows the wrong line,
because breakpoint resolution and stepping use whatever the line table says.

### 3.4 C, include-heavy C++, and `.s`

- **C.** One `S` per `.c`, one `L` per statement/instruction plus the
  definition line. The `.addr` variables, parameters and globals are covered by
  §4.
- **Include-heavy C++.** The IR `DebugLocation` carries a `file` as well as a
  line (`KC/src/irgen/c_irgen.cpp:422-426`); a line record that carries only a
  line number attributes template/inline/header code to the TU's `.cpp` with a
  header line number — wrong file *and* wrong line. If the target supports
  header-defined functions (templates, inline), the source-mapping path must
  carry the file: either the direct-bridge style (nothing to do — `S` per file,
  `L` per instruction) or a file-aware line directive (`.debug_line <file>
  <line>`, with the assembler maintaining one `S` per distinct file and
  remapping ids). A one-file line directive is only sufficient for a target
  whose whole program is one `.c`/`.cpp` plus the runtime.
- **`.s`.** Only KR16, KR216 and KR232 accept assembly input for `-c`
  (`KC/src/tools/krc/main.cpp:839-848`). KR32's assembler accepts the
  `.debug_info` directive but discards its contents
  (`KC/src/asm/kr32_assembler.cpp:843-847`), and `krc image` on a non-object
  input assembles with an **empty** bridge — no `.debug_info`, therefore no
  `.kdbg`, and an adapter that requires `.kdbg` refuses the image
  ([debug-symbols-reference.md](debug-symbols-reference.md) §6.3, §7.3). For a
  new VM: decide explicitly whether assembly input is a supported source kind.
  If yes, the assembler must synthesise `S`/`L`/`F`/`G` (as kr232's does) and
  accept the directives that carry the rest; if no, refuse assembly input in the
  driver rather than producing an image with a misleading or empty debug
  payload.

### 3.5 What a hand-written `.s` can and cannot get

A hand-written `.s` goes through the same assembler as compiler output, so the
same synthesised records appear — but only facts the text carries:

- `S`/`L` from physical lines — correct and sufficient for line debugging.
- `F` for every globally-bound text label; an internal/`.L` label gets no `F`.
- `G` for data labels that the assembler binds globally.
- `V`/`W` only if the assembly carries `.debug_var`-style directives (or the
  assembler learns a `.debug_function`/`.debug_varinfo` directive).
- No `K`, no `P`, no `T`/`C`/`M`/`A`/`R` unless directives provide them; a `V`
  without a `T` shows `?`.

That is the honest subset. Write the subset down in the VM's debugger page and
do not market assembly debugging beyond it.

### 3.6 Source paths

Record the path the build was invoked with, relative to a stable root, using
`'/'`. The linker can rewrite it to `--source-root`-relative
(`KC/src/linker/linker.cpp:818-828`); krdbg resolves relative paths against the
image directory and the configured source roots, and leaves an unresolved path
alone rather than inventing one (`KD/adapters/kr232/kr232_adapter.cpp:590-619`).
Cross-platform fixture images must embed identical paths or the drift check
fails (§9.5). A path containing a space cannot round-trip the bridge (§2.0).

---

## 4. Variables and the promotion policy

### 4.1 Named source variables are IR allocas ending in `.addr`

IRGen creates one `<name>.addr` alloca per parameter and per local, and binds
the source name to it (`KC/src/irgen/c_irgen.cpp:116-123` for parameters,
`:1663-1666` for locals). Every read and write of the source name is a
`load`/`store` through that pointer. Parameters additionally arrive in ABI
registers and are spilled by the backend. Everything below follows from that
one fact.

### 4.2 What to emit for a variable

For each parameter:
1. a `V <function> <param> <frame-offset> <type> <size>` from the backend's
   parameter slot;
2. a `W ... param <function-scope> 0x0`;
3. a `T` for the type (recursively).

For each local:
1. find the alloca whose IR name ends `.addr`;
2. strip the suffix — that is the source name;
3. skip compiler temporaries (below) and skip a name that is already a
   parameter;
4. emit `V` with the alloca's frame offset, then `W ... local ...`.

KR32 is the reference implementation of the filter and the order
(`KC/src/backend/kr32_backend.cpp:1516-1586`): parameters are de-duplicated by
`function.name + "." + name`, locals require the `.addr` suffix, the suffix is
stripped, parameter names are skipped, then `add_debug_type`, `V`, `W`. KR16 and
KR216 are the same shape with their own offset convention
(`KC/src/backend/kr16_backend.cpp:1195-1258`,
`KC/src/backend/kr216_backend.cpp:1833-1878`).

```cpp
// identity: the same string as F.name; display name for C++ member functions.
const std::string& identity = function_display_name(function);

for (const auto& parameter : function.parameters) {
    const auto slot = layout.slots.find(parameter.get());
    if (slot == layout.slots.end() || !parameter->type) continue;
    if (!emitted.insert(identity + "." + parameter->name).second) continue;
    add_debug_type(parameter->type);
    bridge_ += "V " + identity + " " + parameter->name + " " +
               std::to_string(slot->second) + " " +
               type_token(parameter->type) + " " + std::to_string(size_of(*parameter->type)) + '\0';
    bridge_ += "W " + identity + " " + parameter->name + " param " + identity + " 0x0\0";
}
for (const auto& block : function.blocks) {
    for (const auto& instruction : block->instructions) {
        if (instruction->opcode != ir::Opcode::Alloca || !instruction->allocated_type) continue;
        auto name = instruction->name;                       // IRGen: "<source>.addr"
        if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".addr") != 0) continue;
        name.resize(name.size() - 5);                        // a compiler temporary has no home
        if (parameter_names.contains(name)) continue;
        const auto object = layout.alloca_offsets.find(instruction.get());
        if (object == layout.alloca_offsets.end()) continue;
        if (!emitted.insert(identity + "." + name).second) continue;
        add_debug_type(instruction->allocated_type);
        bridge_ += "V " + identity + " " + name + " " + std::to_string(object->second) + " " +
                   type_token(instruction->allocated_type) + " " +
                   std::to_string(size_of(*instruction->allocated_type)) + '\0';
        bridge_ += "W " + identity + " " + name + " local " + identity + " 0x0\0";
    }
}
```

### 4.3 Frame-offset conventions and the sign contract

`V`'s offset is a displacement from the frame base the adapter publishes in
`RawFrame::frame_base`; the session adds it to that base
(`KD/src/core/variables.cpp:78-83`). Three spellings exist:

| Producer | Bridge writes | Adapter reports | Result |
|---|---|---|---|
| KR32 | slot offset from `R7` (positive) | `frame_base = R7` | `R7 + offset` |
| KR232 | `-offset` from `EFP` (negative) | `frame_base = EFP` | `EFP - offset` |
| KR16, KR216 | `frame_size - slot` (positive) | adapter negates at load | `FP - (frame_size - slot)` |

The convention is only half the contract; the adapter's negation is the other
half. The symptom of a mismatch is locals that read as zero or as another
local's value: the address lands in the wrong slot. Validate it with a fixture
whose two locals have different, non-zero values and by expanding a struct
member (member reads go through the same frame base).

KR232's negative form works only because KDBG stores the offset as a `u32` and
krdbg reinterprets it as `int32` (`KC/src/kdbg/kdbg.cpp:163-168`). A new KDBG
consumer is not obliged to do that. For new work, use positive-from-frame-base
(KR32's form) unless the ABI's frame slots are naturally below the frame
pointer, and put any negation explicitly in the adapter with a comment.

### 4.4 Filters: temporaries, aggregates, address-taken

- **Compiler temporaries never get records.** IR temporaries are named
  `call.temp`, `operator.temp`, `subscript.this`, and C++ object temporaries
  `temporary.*`; only `<name>.addr` allocas are source variables. Recording a
  temporary means a later pass may promote it and leave a `V` pointing at a dead
  slot (`KC/docs/symbols.md:195-199`, `KC/docs/optimize.md:415-417`). A source
  variable is deliberately *not* promotable, so its `V` stays true.
- **Aggregates and address-taken objects stay in memory.** An object whose
  address escapes cannot be promoted, by the promotion passes' preconditions
  (E4/E6 in `KC/docs/optimize.md:415-417`) and by the safety contract
  (`:486-487`). Their `V` + `M`/`A` records are truthful. If a future pass ever
  promotes one, the `W` address-taken flag (bit 0) is the existing way to signal
  that a register location is the only valid reading.
- **Parameters are always allocated a slot** (`KC/src/backend/kr32_backend.cpp:325-330`);
  their `V` is truthful at every pc.

### 4.5 Promotion at `-O2` and the `P`/`Locations` option

Promotion of recorded allocas is a **per-target policy**, not a level-only rule
(`KC/src/toolchain/toolchain.cpp:686-699`; the pass's refusal is
`KC/src/ir/passes.cpp:331-353,585-605`). kr232 and kr216 pass `false` at every
level, so a named variable keeps its home slot and the peephole/dead-store passes
keep stores to a recorded slot. kr32 and kr16 still enable it at `-O2`: a
promoted `<name>.addr` disappears and with it the only `V` record for that
variable, so a debugger shows nothing or, on a backend that records a slot no
longer written, a stale value. krdbg's per-target defaults are `-O2` for kr232,
`-O1` for kr32/kr16, and `-O0` for kr216 at bring-up. kr232's `-O2` caveat is
inlining rather than variable loss: small internal single-block helpers are
inlined, so an inlined helper has no frame/F record and its locals are absent;
`-O1` is the faithful-stepping override. At `-O0`/`-O1` every named variable
keeps its home on every current target.

The format's answer is `P`/`Location` (v1.1, additive; ADR 0009's amendment at
`KC/docs/decisions/0009-kdbg-v1-freeze.md:43-64`, ADR 0052). The policy
recommendation in `KC/docs/optimize.md:85-89` and §10 (`:550-570`) is:

- **A — promote temporaries only, now.** Named locals and parameters keep their
  slots. No format work; every `V` stays true. This is what the tree implements
  at `-O0`/`-O1`; the `-O2` gate (below) is the part that does not hold
  everywhere yet.
- **C — extend KDBG when a backend genuinely keeps a named local in a
  register.** Consumer first, then producer (§2.8). Never B ("promote and
  document the loss").

A new backend inherits option A by doing nothing special: the promotion passes
are target-independent and refuse `.addr`. But it must also not *undo* the
protection through a target-specific pass. KR232 demonstrates the hole: two
`-O1` text peepholes delete frame stores using machine liveness that does not
know a slot was named by a debug record
(`KC/src/backend/kr232_backend.cpp:2647-2660`;
`KC/src/backend/kr232_cfg.cpp:416-505`, `:507-564`). The variable is listed, but
its home slot is empty — a silent wrong value. If a backend runs any pass that
deletes or reuses frame stores after debug records are chosen, that pass must
treat every slot named by a `V` as live at every point (the "recorded-slot set"
rule in [register-promoted-locals.md](register-promoted-locals.md) §3.2).

### 4.6 The policy a new backend adopts

1. At `-O0`/`-O1`: emit `V`/`W` for every parameter and every `<name>.addr`
   local; filter temporaries; keep the slot stable for the whole function.
2. At `-O2`: if the target's pipeline can promote recorded allocas, either
   disable recorded promotion for the target (target capability flag in
   `toolchain.cpp`) or implement the full `P` producer+consumer path. Do not ship
   a level where the Variables window silently empties.
3. Never emit a `P` record that is not true at every instruction in its range.
4. Test the promise: a fixture with a local that is assigned twice, one that is
   assigned and never read, one that is address-taken, and a static function
   (§9.3, `promoted.cpp`).

---

## 5. Call stack and unwinding

### 5.1 Who owns the walk

The adapter owns the frame walk: `IDebugTarget::unwind()` returns
`RawFrame{pc, frame_base}` innermost first (`KD/include/krdbg/target.h:97-100`,
`:190`). KDBG supplies only what the walk cannot know from the machine:
function bounds (`F`), names (`F.name`) and, for ABIs whose chain is described
by a frame size, `F.frame`. The session then labels each frame with
`function_at(pc)` and uses it for `locals()` and step-out
(`KD/src/core/session.cpp:626-643`, `:438-456`, `:520-543`).

### 5.2 Function bounds and return addresses

Two properties are load-bearing:

1. **Every return address must lie inside its caller's `[start, end)`.** A call
   site's return PC is the instruction after the call; the caller's `F` range
   must cover it, or the stack stops at the caller (`KD/adapters/kr32/kr32_adapter.cpp:76-98`).
2. **The innermost frame's PC must also be inside a function**, or the stack is
   empty. A pc in a function's epilogue is the usual counter-example when the
   `F` end label precedes the epilogue; the frame cannot be named and the walk
   stops (§6.3).

Decide the end-label policy and test a pc in the epilogue deliberately: either
it resolves to the function (end after epilogue, kr232) or the debugger honestly
reports no frames (end before epilogue, KR32/KR16/KR216). Both are defensible;
silently having a range that stops *inside* the body is not.

### 5.3 The two frame-chain shapes in the tree

- **Uses `F.frame` (KR32).** The prologue places the saved `R7` at `R7 + frame`
  and the return PC at `R7 + frame + 4`; the adapter reads those two words and
  continues at the caller's pc (`KC/src/backend/kr32_backend.cpp:1328-1346`;
  `KD/adapters/kr32/kr32_adapter.cpp:76-98`). `F.frame` must be the emitted frame
  size and stay in step with the `.cpp_except` descriptor's frame size
  (`KC/src/backend/kr32_backend.cpp:1611-1626`). If a target unwinder (the VM's
  own C++ unwinder) also relies on that descriptor, one source of truth for the
  frame size is mandatory.
- **Fixed-layout chain, ignores `F.frame` (KR16, KR216, KR232).** The chain is
  at fixed displacements: KR16 reads saved `R4` at `[R4]` and the return PC at
  `[R4+2]` (`KD/adapters/kr16/kr16_adapter.cpp:148-171`); KR216 reads saved
  `R13`/return PC at `[R13]`/`[R13+2]` (`KD/adapters/kr216/kr216_adapter.cpp:101-123`);
  KR232 reads saved `EFP`/return PC at `[EFP]`/`[EFP+4]` and terminates on a
  self-referential next pointer (`KD/adapters/kr232/kr232_adapter.cpp:298-338`).
  These still write a frame size in `F.frame` where the backend knows one
  (KR16 and KR216 write `layout.frame_size`), but their adapters do not read it;
  KR232 writes `0 0` because its `F` is synthesised by the assembler, and that is
  fine for the same reason.

When no frame has been built yet — reset, kernel entry, the first instructions
of a program — publishing the current position as frame 0 keeps the stack panel
named and `locals(0)` working; the session treats a single frame as "no caller"
for step-out (`KD/adapters/kr232/kr232_adapter.cpp:320-336`). Never publish a
synthetic frame with `pc == 0` or a `frame_base` that was not measured: it reads
as a caller that does not exist and makes every unwind-based query lie.

A new VM picks one, and the choice must agree on three things: what the prologue
writes, what the VM's exception/backtrace code expects, and what the adapter's
`unwind()` reads.

### 5.4 The step-over depth problem

The session's step-over uses the frame depth where it can (`target->unwind()`),
but a chain built by a `CALL`-less prologue changes depth a step late. The
session therefore also treats "the pc's function changed" as entering a call
(`KD/src/core/session.cpp:406-437`, comment at `:408-410`: kr232 builds its
chain in `ENTER`, not at `CALL`). This fallback needs truthful `F` ranges: if
`function_at` cannot tell the callee from the caller, step-over can run away
until the guard. A backend that emits no `F` for internal-linkage functions
makes step-over inside a static function misbehave precisely here
([symbol-verification-plan.md](symbol-verification-plan.md) §12.2).

### 5.5 Display names versus symbols

Frames are named with `FunctionInfo::name`, which is the `F` display name; that
name is not unique (`Class::method` for two overloads), and anything that
resolves continues to use the symbol (`KC/docs/symbols.md:65-70`). Two
consequences:

- Do not use the display name as a label to resolve; `F`'s second field is the
  symbol and the linker resolves that.
- Do not mix the two spellings without a bridging record: `locals()` matches the
  variable's function string against the function's identity. Use the display
  name in `V`/`W` (kr216's shape, which needs no `K`), or keep the symbol in
  `V`/`W` and emit the function-scope `K` that names it so the working-tree
  reader can derive `FunctionInfo::symbol` (`KC/src/backend/kr216_backend.cpp:1845-1876`,
  `:1830-1832`); a payload that names the symbol with no `K` was why
  KR32's C++ member-function locals disappeared at HEAD
  (`KC/src/backend/kr32_backend.cpp:1535-1539`; [debug-symbols-reference.md](debug-symbols-reference.md) §6.1).

### 5.6 Unwind pseudocode (adapter side)

```cpp
std::vector<RawFrame> unwind() const override {
    std::vector<RawFrame> frames;
    std::uint64_t fp = frame_pointer_register();
    std::uint64_t pc = current_pc();
    while (fp != 0 && frames.size() < kFrameCap) {
        // A frame is published only when both words can be read; otherwise the
        // caller is unknown, not frame 0 with pc=0.
        std::uint64_t saved_fp = 0, return_pc = 0;
        if (!read_word(fp + kSavedFpOffset, saved_fp)) break;
        if (!read_word(fp + kReturnPcOffset, return_pc)) break;
        frames.push_back(RawFrame{pc, fp});
        if (return_pc == 0 || saved_fp == fp) break;
        pc = return_pc;
        fp = saved_fp;
    }
    if (frames.empty()) frames.push_back(RawFrame{current_pc(), stack_pointer()});
    else frames.front().pc = current_pc();   // frame 0 is where we are, not where we return
    return frames;
}
```

The same rules as KR232's implementation apply: read both words before
publishing a frame, terminate on a zero or self-referential link, and keep the
innermost frame's pc the current pc (`KD/adapters/kr232/kr232_adapter.cpp:298-338`).

---

## 6. Common failures observed

Every row is a defect that has been seen in this tree (or in the KR32 precedent
it mirrors), with the symptom a debugger shows and the fix. The fixes in the
right-hand column are the ones that pass the validation playbook.

| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | Debugger shows a line that does not exist in the file (e.g. line 240 of an 8-line file); breakpoints pass tests but stop in the wrong place | generated-assembly line numbers recorded as source lines | line directives before each instruction **and** before the prologue; no fallback to assembly lines for generated code (§3.3) |
| 2 | A function's definition line has no breakpoint; stepping starts after the prologue | no `L` at `.L<symbol>.begin` | emit the entry label and the definition-line section (ADR 0061, §2.2) |
| 3 | C++ member-function locals missing from the Variables window | `V.function` (symbol) ≠ the function's identity (display name / no `K` to bridge them) | use one name everywhere, or display name in `F` plus symbol in `V`/`W` and a function-scope `K` naming the symbol (§6.1) |
| 4 | `static` function's locals missing; its lines vanish; call stack stops at it | no `F` for internal-linkage functions, so the linker drops its lines and `function_at` is null | emit `F` for every defined function, not only global symbols; unique labels (§6.2, §7) |
| 5 | Two `static int counter;` in two objects read the same address; two `static` helpers share a range and lines | linker symbol map keyed by bare name, first wins | unique per-object labels for internal-linkage `F`/`G`/line labels (§6.2) |
| 6 | Locals read zero or another local's value | frame-offset sign or frame-base mismatch | one documented convention + adapter negation; test two locals and a struct member (§4.3) |
| 7 | Every variable listed as a local; parameters not marked | no `W` records | `W` per `V`, with `param`/`local`/`static` (§2.6) |
| 8 | Compiler temporaries (`call.temp`, `operator.temp`, `subscript.this`, `temporary.*`) listed as locals; a later promotion leaves a stale `V` | all named allocas recorded | require the `.addr` suffix, strip it, skip parameter names (§4.2) |
| 9 | A local is listed but reads stale/zero | a target-specific pass deleted its frame store after debug records were chosen | recorded slots are live at every point in the pass's liveness model ([register-promoted-locals.md](register-promoted-locals.md) §3.2) |
| 10 | Variables empty at `-O2`, no "optimized out" | recorded allocas promoted; `V` keyed on the alloca | keep recorded allocas unpromoted, or implement `P`+consumer (§4.5) |
| 11 | A `G` missing for a file-scope `static` global; internal globals absent from the list | global loop filtered by `.globl`, or skipped internal linkage | include definitions of every linkage; `G` with real size (§2.4, §7) |
| 12 | Two objects' line labels resolve to one address; a function span looks empty | local line labels named per object (e.g. `.L0`), merged by name | hash/path-prefix per object (kr232 hashes the text: `KC/src/asm/kr232_assembler.cpp:522-547`) |
| 13 | A `C`/`M`/`A`/`R` type prints `?`, a member has no size, or a struct shows duplicated fields | no `T` for the referenced type, a type name spelling mismatch, or each TU emitting the same type's members again | emit `T` for every referenced type, same spelling everywhere, dependency first; the linker de-duplicates members by name (`KC/src/linker/linker.cpp:954-970`), so per-TU emission is tolerated but still needs deterministic order (§2.9) |
| 14 | Image refuses to load, or symbols from a different build appear | missing `.kdbg` (adapter policy), `image_base` mismatch, stale sidecar | decide the missing-payload policy; record the base; compare `build_id` for sidecars (`KC/include/krc/debug/kdbg.h:171-180`) |
| 15 | Header/template code shows the `.cpp` file with a header line number | line directive carries no file | carry the file (`S` per file in style A; file-aware directive in style B) (§3.4) |
| 16 | A breakpoint at the function end never binds; the epilogue has no frame; a line record resolves inside the next function | `F` end before the epilogue, or an `F` span crossing into the next function because its end label was inferred from a label heuristic | choose the end-label policy deliberately and test a pc in the epilogue and a return PC in the body (§5.2, §6.3) |
| 17 | A hand-written `.s` image has no symbols, no functions, or will not load at all | the assembler has no debug path: it discards `.debug_info` (KR32) or synthesises nothing (empty bridge), or it emits `F`/`G` only for globally-bound labels | decide the assembly-source policy; synthesise `S/L/F/G` from the text; add directives for `V/W`/types; refuse assembly input where it cannot be represented (§3.4–3.5) |
| 18 | Line records appear under the wrong source file, or an object's lines all collapse onto one file | an object reused one `S` id for two paths, or emitted an `L` before the `S` that introduced its id, so the linker's per-object remap could not apply | one `S` per distinct path, ids unique within the object, `S` before every `L` that uses its id (§2.0, §2.1) |

### 6.1 The name contract (`F.name`, `V.function`, `W.function`, `K`)

`locals(frame)` finds the frame's function by pc and then keeps variables whose
`function` equals that function's **identity**. At HEAD `71e4591` the identity
is `FunctionInfo::name`, i.e. the `F` display name
(`KD/src/core/session.cpp:654-667`); the working tree adds
`FunctionInfo::symbol` and `identity()` = `symbol.empty() ? name : symbol`, and
derives `symbol` from the function-scope `K` record when the payload's `V`/`W`
records name the function by it (`KD/include/krdbg/debug_info.h`,
`KD/src/kdbg/kdbg.cpp`). `W` is matched to `V` by `(function, name)` after
translation (`KD/src/kdbg/kdbg.cpp:150-187`).

The failure this prevents is real: KR32 writes the mangled symbol in `V`/`W`
and the display name in `F`, so `Counter::add` had no locals in krdbg even
though they existed in the image
([debug-symbols-reference.md](debug-symbols-reference.md) §6.1). KR216 writes
the display name in `V`/`W`, which is the variant that works without `K`
(`KC/src/backend/kr216_backend.cpp:1845-1876`); note its `K` still uses the
symbol (`:1830-1832`), which is exactly what the working-tree reader needs if a
future consumer relies on identity. The old `members(VariableView)` lookup by
function name is replaced in the working tree by the resolved location carried
on the view.

Two producer rules are acceptable; pick one and be consistent:

1. **One name.** Use the display name in `F.name`, `V.function`, `W.function`,
   `K.function`, `P.function`. The symbol appears only in `F`'s second field and
   in labels. This is kr216/kr232's shape and needs no `K`.
2. **Display name plus symbol.** `F.name` is the display name, `V`/`W`/`P` name
   the function by its symbol, and a function-scope `K` record names the symbol
   with `start` equal to the function's `F.start`. The working-tree reader then
   sets `FunctionInfo::symbol` and compares against it. Do not mix the two
   spellings without that `K`; a mismatch is silent absence, not an error.

### 6.2 Internal linkage and bare-name collisions

The bridge resolves labels by name against `image.symbols`, which contains local
symbols too, and `attach_kdbg` inserts into an `unordered_map` where the first
insertion wins (`KC/src/linker/linker.cpp:542-563`, `:758-761`). Consequences
measured on a two-TU probe: two `static int helper(...)` definitions produced
two `F` records with the *same* range and both units' `.Ldbg` labels collided,
so both units' lines named one body; two `static int counter` globals both
resolved to the first object's address
([debug-symbols-reference.md](debug-symbols-reference.md) §6.2, Evidence rows
5–6).

The fix is producer-side: every label that can appear in more than one object
must be unique. Use the function symbol for real functions (the assembler/object
already treats `static` functions as local but still named), and a per-object
prefix (file hash or object-name slug) for the synthetic labels (line labels,
`begin`/`ret`/`end` labels, local data labels). The kr232 assembler hashes the
input text for its line labels and skips CR so a CRLF checkout produces the same
labels as an LF one (`KC/src/asm/kr232_assembler.cpp:522-547`); its text labels
are still bare names (`:551-560`), which is why static functions are the
remaining hole there.

A new VM should decide the label namespace up front: real symbols get uniqueness
from the language's linkage rules except for `static`, and synthetic/line labels
get none — always prefix them per object.

### 6.3 The `F` end label (`ret` vs end-of-epilogue)

Two consequences follow from where `F` ends:

- If `F` ends at `.L<symbol>.ret` (before the epilogue), a pc in the epilogue
  belongs to no function: `function_at` is null, `line_at` falls back to the last
  line before the pc, and the call stack stops at the innermost frame. On KR32
  that is observable: function `[0x10000, 0x10094)` while the scope is
  `[0x10000, 0x100a0)` ([debug-symbols-reference.md](debug-symbols-reference.md) §6.4).
- If `F` ends after the epilogue (KR232's synthesised end at the next function
  label/end of `.text`), the epilogue is covered and a stop there still names
  the function.

Either is acceptable if documented; the failure to avoid is an `F` end that is
inside the function's body or an end label that resolves to a later function's
start, because then the line filter and `function_at` disagree about the body.

### 6.4 The builder's debug-location state leaks into the next function's entry

IR builders commonly keep the current source location as builder state, and
statement emission updates it. Function entry code — parameter spills and
implicit member/base initializer calls — is emitted before the first statement,
so it inherits whatever location the previous function happened to leave behind.
The symptom is subtle: a constructor's prologue maps to the *previous* inline
function's header line, so source stepping from the constructor's opening line
jumps into an unrelated header. The durable fixes are (a) reset the builder's
location to the function definition before emitting entry code, and (b) when a
function has no definition location, emit an explicit "no source line" marker
rather than letting the previous line persist. Regression check: the first
`L` record of every function must name the function definition's file and line,
and no record in a function body may name a file other than the definition's
unless the body genuinely contains that source (an inline body defined there).

---

## 7. KR232 lessons (target-specific; skip on first read)

> **BOX — KR232 lessons (2026-11-27).** Everything in this section is specific
> to the kr232 text-assembly path and the work in flight. Read it as a worked
> example of §3.2/§3.3 and as the state a port from kr232 starts from, not as a
> requirement for every target.

**How kr232 produces symbols in the current tree.** The backend emits only text
plus directives (`.debug_type`, `.debug_global`, `.debug_var`, `.debug_line`) and
the assembler synthesises the bridge:

- `.debug_type` records are collected before code and emitted at the top of the
  assembly (`KC/src/backend/kr232_backend.cpp:1384-1424`, `:1426-1462`); the
  assembler forwards them as `T/C/M/A/R` (`KC/src/asm/kr232_assembler.cpp:158-161`).
- `.debug_global <name> <type> <size>` gives globals a real type and size
  (`KC/src/backend/kr232_backend.cpp:1508-1513`), so a data label does not
  become an `F` record spanning the text
  (`KC/src/asm/kr232_assembler.cpp:548-550`).
- `.debug_var` carries each named alloca's home offset and becomes `V`
  (`KC/src/backend/kr232_backend.cpp:1763-1788`;
  `KC/src/asm/kr232_assembler.cpp:146-151`, `:586-588`).
- The assembler emits one `S 1 <recorded source>`, one `L` per instruction from
  a per-object-hashed label prefix, `F` for globally-visible text labels with
  `0 0` for frame/return type, and `G` for globally-visible data labels
  (`KC/src/asm/kr232_assembler.cpp:510-592`).

**What the working tree already does.** The uncommitted `.debug_line` change
makes the backend emit a line directive before the body and whenever the IR line
changes (`KC/src/backend/kr232_backend.cpp:1975-1986`), and the assembler honours
it instead of the generated-assembly line
(`KC/src/asm/kr232_assembler.cpp:111-157`, `:276-292`, `:411-412`). Type records
(`ff72fce`) and sized globals are committed. On a rebuilt startrekng image this
turns 95% out-of-range line records into 0.17% (55/32,307); the remaining defect
is the definition line, the cross-function line-state leak, and the F/G/V/W/K
half below ([kr232-debug-symbols-gap.md](kr232-debug-symbols-gap.md)).
The krdbg working tree supplies the consumer half: `P` translation,
function-identity from `K`, and `VariableSlotUnusable`/`<optimized out>`.

**Remaining measured gaps** (ranked in [kr232-debug-symbols-gap.md](kr232-debug-symbols-gap.md)
as G1–G12; this table is the durable summary, with the section of this guide each
gap maps to):

| Gap (gap doc) | Observed | Fix |
|---|---|---|
| G1: no definition-line record; wrong entry line | `t2.c` entry record is generated-assembly line 7; `t1.c`'s `__krc_user_main` (definition line 10) has no line-10 record at all; 248/328 F ranges in a rebuilt startrekng image begin with the previous function's last line; 55/32,307 records out of range | emit the definition line's directive immediately after `.L<name>.begin` and before `enter`; add an explicit "no source line" directive form for synthetic functions so state cannot leak; keep the hand-written `.s` fallback (§3.3) |
| G2: `F` only for `.globl`/`.weak` functions | 44 functions without F in the rebuilt image (12 in the example TUs, 32 in `libc_full.c`); their lines are then dropped by the linker, so no breakpoint or frame can name them | emit the function record from the backend (`.debug_function <display> <begin-label> <end-label> <frame> <ret-type>`) and synthesise per-object begin/end labels in the assembler; keep the `.globl` heuristic only for hand-written `.s` (§2.3, §6.2) |
| G3: no `debug_name` | call stack shows `__krc_user_main`, `Widget_get`, `Computer_dtor` | carry the IR display name in the function directive/F record; the krdbg consumer already derives identity from `K` (§2.3, §6.1) |
| G4: duplicate/stale F/V records across TUs | 8 duplicate destructor names ×5, 34 duplicate V records, a stale span `0x5fe0-0x98dc` for weak inline definitions | per-object labels for F bounds; emit one F/V set per surviving definition, or let the linker/first-wins rule be the documented policy (§6.2) |
| G5: `G` only for `.globl`/`.weak` data | every file-scope static is absent (`libc_full.c` statics, `navigationDx`, `kRegionSuffixes`) | the assembler's global set must include every `.debug_global` label, not only `.globl` (§2.4) |
| G6: `V` without `W`/`K`; parameters and `this` indistinguishable | `variableInfo: []` for every kr232 image; temporaries listed | add `.debug_varinfo`/`.debug_scope` forwarding, the `.addr` filter, and a parameter `V`/`W` loop as KR32 does (§2.6, §4.2) |
| G7: promoted locals disappear; no `P` | at default `-O1` a shadowed named local can still be promoted; at `-O2` scalars/params lose their `V`; no `P` anywhere | protect recorded allocas from the text peepholes and from promotion until a `P` producer exists; krdbg consumer is in the working tree (§4.5, [register-promoted-locals.md](register-promoted-locals.md) §3.2) |
| G8: split `-S`/`-c` mixes generated `.s` path with C lines | breakpoints/stepping fail in that workflow | the line directive must carry the source file as well as the line, or the split workflow is declared unsupported (§3.4) |
| G9: `F` frame/return type placeholders, no end label | `frame=0 type=0`; end comes from the next text label | pass real frame/return type; emit `.L<name>.end` after the epilogue (§2.3) |
| G10–G12: one `L` per instruction, column 0, no function source/line | ~2× KR32's line table; column always 0; `FunctionInfo.source/line` never set | size only, no krdbg consumer today; leave until a consumer needs it |

A port that starts from kr232 should treat this table as the known work list and
the rest of this guide as the contract it is trying to reach.

---

## 8. Porting checklist (kr216 and a new VM)

Work in this order; each step has a check that does not need the debugger.

### 8.1 Ordered phases

| Phase | Work | Files/classes | Check |
|---|---|---|---|
| 1. Metadata | Confirm IR carries what the records need: `DebugLocation definition` on `ir::Function` (ADR 0061), `debug_name` for C++ display names, `DebugLocation{file,line,column}` on instructions | `KC/src/irgen/c_irgen.cpp`, `KC/include/krc/ir/ir.h` | IR dump shows file/line on statements and a definition location |
| 2. Backend records (style A) or directives (style B) | types first, then `S`/`L`/`F`/`K`, `V`/`W`, `G`, labels | `KC/src/backend/<vm>_backend.cpp` (class `Kr<n>Backend`: `add_debug_type`, `add_debug_line`, `emit_function`, `emit`), `KC/include/krc/backend/<vm>_backend.h` | `krc --dump-symbols` on a linked image shows the record counts you expect |
| 3. Assembler (style B only) | parse the new directives, forward the records; synthesise `S`/`L`/`F`/`G` from the text; unique label prefix; line-mapping policy | `KC/src/asm/<vm>_assembler.cpp`, `.h` | assembler unit test: a `.debug_line N` yields line N; a non-`.globl` `.debug_global` yields `G`; a `static` function yields `F` |
| 4. Toolchain wiring | append the bridge for style A; pass the recorded source name and do **not** double-append for style B | `KC/src/toolchain/toolchain.cpp:169-181`, `:238-322` (`to_target_id`/`assemble_impl` branch), `:734-780` (backend branch) | `krc -c` then `krld` then `--dump-symbols` shows one source and no duplicate records |
| 5. Linker | use the existing letters; only a new record kind needs `attach_kdbg` parsing + KDBG encoding + a tag in `docs/symbols.md` | `KC/src/linker/linker.cpp:742-1034`; `KC/src/debug/kdbg.cpp`; `KC/include/krc/debug/kdbg.h` | existing v1.1 unit tests stay green; hand-built bridge test for the new letter |
| 6. krdbg | translate new records in `KD/src/kdbg/kdbg.cpp`; adapter `unwind()`, `frame_base`, register groups, source roots, payload discovery; container handling | `KD/src/kdbg/kdbg.cpp`, `KD/include/krdbg/debug_info.h` (interface freeze: update `PLAN.md` first), `KD/adapters/<vm>/<vm>_adapter.cpp`, `KD/src/host/build_chain.cpp` | session-level tests below |
| 7. Tests and fixtures | producer unit tests, one session test per requirement, handwritten and generated fixtures, drift check | `KC/tests/unit/test_kdbg*.cpp`, `KC/tests/unit/test_toolchain.cpp`; `KD/tests/test_<vm>_adapter.cpp`, `KD/tests/test_session.cpp`, `KD/tests/fixtures/<vm>/` | gate with the adapter preset **on** |
| 8. Docs | `KC/docs/symbols.md` for a format change; ADR; the VM's debugger page; update this guide's kr232 box if the port changes the general recipe | `KC/docs/decisions/`, `KD/docs/` | a reader who has not seen the change can reproduce the work |

### 8.2 kr216 specifically

The kr216 backend is style A and already closer to the contract than kr232:

- `add_debug_type` emits `T/C/M/A/R` (`KC/src/backend/kr216_backend.cpp:457-489`);
  `add_debug_line` emits `S`/`L` from the IR location (`:491-501`); the entry
  label and definition-line record exist (`:670-677`); per-instruction labels
  exist at `:748-756`; `F`/`K` are at `:1824-1832`; `V`/`W` for parameters and
  `.addr` locals at `:1833-1878`, using the **display name** (the krdbg-compatible
  variant); local frame offsets use the `frame_size - slot` form and the krdbg
  kr216 adapter negates them at load (`KD/adapters/kr216/kr216_adapter.cpp:371-376`);
  `G` includes internal linkage (`KC/src/backend/kr216_backend.cpp:2019-2026`).
- `-O1`/`-O2` use the same target-independent promotion policy; recorded allocas
  are refused below `-O2`, so the home-slot promise holds at the default level.
- No `P` exists and none is needed while the promise holds.

So "apply the kr232 work to kr216" means: verify each row of §6 against a
regenerated kr216 image, and fix the deltas the verification finds. The two
already-visible deltas are (a) `K` uses the symbol while `F`/`V`/`W` use the
display name (`KC/src/backend/kr216_backend.cpp:1830-1832`) — harmless to krdbg
today, wrong for a future consumer; and (b) `F` ends at `.ret`, so the epilogue
is outside the function (§5.2) — decide whether that is the intended policy.
Add the fixture matrix in §9.3 for kr216 (there is no kr216 symbol-verification
plan yet) and run its gate with the kr216 adapter option on.

### 8.3 Files to touch, mapped

| Concern | KRCompiler | krdbg |
|---|---|---|
| Types | `<vm>_backend.cpp` `add_debug_type` | none (translated generically) |
| Lines and labels | `<vm>_backend.cpp` `add_debug_line`, function/block labels | none |
| Functions | `<vm>_backend.cpp` `F`/`K` emission; `<vm>_assembler.cpp` (style B) | adapter `unwind()` |
| Variables | `<vm>_backend.cpp` parameter/local `V`/`W`; frame layout | `src/kdbg/kdbg.cpp` (already generic), adapter `frame_base` and offset sign |
| Globals | `<vm>_backend.cpp` `G` loop; assembler (style B) | none |
| Promotion | `toolchain.cpp` gate, `passes.cpp` refusal, target `P` producer | consumer in the working tree (`src/kdbg/kdbg.cpp` translation, `src/core/session.cpp` PC-aware location); producer is future work |
| Payload discovery | linker container handling | `adapters/<vm>/<vm>_adapter.cpp` (`read_section` or `read_payload`) |
| Tests | `tests/unit/test_kdbg.cpp`, `test_kdbg_v11.cpp`, `test_toolchain.cpp` | `tests/test_<vm>_adapter.cpp`, `test_session.cpp`, `tests/fixtures/<vm>/`, `CMakeLists.txt` test registration |

---

## 9. Validation playbook

Two questions must be answered separately: **is the payload truthful?** (a
producer test, cheap, no VM) and **does the debugger behave?** (a session test,
needs the adapter and a fixture). A debugger suite alone cannot catch a
line-table lie, and a payload suite alone cannot catch a consumer regression.

### 9.1 Producer assertions (no VM, `--dump-symbols`)

Run on every fixture image at `-O0` and `-O1`:

1. **Source lines are real.** For every `S` file that exists, every `L` line
   with that source id is in `1..line_count`. This is the invariant that catches
   the masquerade; measured failure examples are in §3.3.
2. **Every line is in a function.** Every `L` address lies in some `F`
   `[start,end)`. The linker enforces this for emitted images, but the producer
   test catches it before the linker silently deletes records.
3. **Function bounds are sane.** `start < end`, no two functions of the same
   object overlap in the body, `start` carries the definition-line record, and
   every return PC of every call site is inside the caller's range. In addition,
   the first `L` of a function must not be the previous function's last line
   (the line-state-leak symptom, §3.3), and a synthetic function with no source
   location must not inherit a line at all.
4. **Variables pair up.** Every `V` has a `W` of the same `(function,name)`; every
   `W` kind is `param|local|static`; no variable name is a compiler temporary;
   every `V.function` has exactly one `F.name`.
5. **Offsets are in frame.** `|offset| + size <= frame_size` for every variable
   of a function whose `F.frame` is meaningful (and for the negative convention,
   `offset <= 0`).
6. **Types resolve.** Every type named by a `V`/`G`/`M`/`A`/`R` has a `T` with a
   non-zero size where a size is required; every `T` size agrees with the size a
   member/array element computation needs.
7. **Sources are unique.** No two `S` records share an id or a path in one
   payload; ids are dense (not required, useful).
8. **Determinism.** Compile the same source twice; the image bytes are
   identical. No timestamps, no host paths, identical argument order.
9. **Level identity for variables.** `-O0` and `-O1` produce the same `V`/`W`
   sets for named variables (this is the `-O1` home-slot promise); the line
   tables may differ by the lines passes removed.
10. **The `-O2` policy is explicit.** Either the same variable records survive,
    or `locations` carry the ranges, or the level is rejected/undocumented in
    the VM's debugger page.

### 9.2 Consumer assertions (session level)

Per requirement, drive the real adapter and assert through the session API
(`KD/include/krdbg/session.h`), not by decoding KDBG in the test:

| Requirement | Assertion |
|---|---|
| Source lines | `add_line_breakpoint(file, line)` resolves; running stops with `line_at(pc)->line == line`, `source == file` |
| Line stepping | `LineOver`/`LineIn` stop only on records that name real lines of the current file; `LineIn` crosses into a callee and `LineOver` does not |
| Assembly lines | the same for a hand-written `.s` image, against physical lines |
| Locals and parameters | `locals(0)` lists the function's source variables with values; `info().variables` flags parameters as `VariableParam`; no `temporary.*` name |
| Caller frame | `locals(1)` reads the caller's variables through the caller's `frame_base` |
| Globals | `globals()` lists external and internal globals with types/sizes/values; `members()` expands aggregate globals |
| Call stack | `call_stack()` names every frame, each `pc` is inside its `FunctionInfo`, and each frame's `line_at(pc)` resolves to its own source file |
| Types | locals/params/globals with struct/array types expand to named members with correct offsets and values |
| `this` | a member function's frame lists `this` |
| Promotion at `-O1` | a promotion-shaped local is listed at both stops of its live range with the right value; `info().variables` show `FrameOffset`, never `Register` |
| Register locations (producer future; consumer in working tree) | a payload with a `Location` becomes a `LocationInfo` range; at a pc inside it the value comes from the named register, outside it the declared home or `<optimized out>`; a register value in a caller frame reports unavailable, not the current register |

The exact kr232 assertions and their names are in
[symbol-verification-plan.md](symbol-verification-plan.md) §7–§8; re-use the
vocabulary rather than inventing a second one. They are implemented in
[krdbg `tests/test_kr232_symbols.cpp`](../tests/test_kr232_symbols.cpp): one
case per row above, driven through a real `DebugSession` over the
`symbols_*`/`promoted_o1` fixtures. The `kr232_symbols_phase2_*` cases carry the
internal-linkage rows (a `static` function's frame and file-scope statics); they
went green when the producer gained an `F` record for every function and `G`
records for internal data.

Two consumer defects that only a frame-aware test finds: a frame's PC and its
`frame_base` must come from the same unwound link (pairing one frame's return
address with the next frame's base names the grandcaller and reads the wrong
locals for frame 1), and `members()` on a member view only walks struct
members, so an array member expands its elements only at variable level, not
member level.

### 9.3 Fixture matrix

All fixture sources are compiled from a root with forward-slash relative paths
and checked in with their generated images; `regenerate.ps1` +
`CheckFixtureDrift.cmake` prove the image still matches the source.

| Fixture | Purpose | Records exercised |
|---|---|---|
| single-file C (`-O0`, `-O1`) | parameters, locals, struct, global, line stepping | `S L F V W T C M G` |
| `symbols_cpp.cpp` (single file) | C++ params/locals, `this`, aggregate parameter, both linkages | add `C` base + `W kind` and the name contract |
| `symbols_pair` (two TUs + header) | cross-unit calls, a local `static` function, a `static` global, caller-frame locals | internal linkage, unique labels, `G` for statics |
| `symbols_asm.s` | hand-written assembly lines and a data object | `S L F G` + `.debug_var`-style local |
| `include_heavy.cpp` | header-defined types and inline code | file identity in line records |
| startrekng (existing multi-file C++ image) | real-world include-heavy line table and frames | everything at once |
| `promoted.cpp` (`-O1`) | a promotion-shaped local across its live range, address-taken local, caller-frame read | `-O1` home-slot guarantee |
| `-O2` probe | promotion policy is explicit | no stale value: either records survive or the level is unsupported |

`symbol-verification-plan.md` §5 gives these fixtures as sources; `§7` gives the
per-fixture assertions.

### 9.4 Commands

Windows (Developer PowerShell for VS 18). The bare presets build with every
adapter **off**, so symbol work needs the adapter preset or
`build-debugger.ps1`:

```powershell
# producer: compile to an object, link a container, dump the payload.
# Link the target's crt0 too when the entry symbol is `_start`; a probe whose
# source defines `main` can instead use `--entry=main`.
krc -c --target=kr232 -O0 -I sysroot/kr232/include src.c -o src.kro
krld --entry=_start crt0.kro src.kro -o src.kr232
krc --dump-symbols --json src.kr232      # inspect; capture counts for the matrix

# consumer: the adapter-enabled build and its tests
powershell -File scripts/build-debugger.ps1 -Clean
ctest --test-dir build/kr232-debugger -R "kr232" --output-on-failure

# fixtures: rebuild from source and prove byte identity
$env:KRC = '...\krc.exe'; $env:KRLD = '...\krld.exe'; $env:KR232_SOURCE = '..\kr232'
./tests/fixtures/kr232/regenerate.ps1
ctest --test-dir build/kr232-debugger -R kr232_fixtures_match_source --output-on-failure
```

WSL x64: build on the ext4 mirror, never on `/mnt/c` (`AGENTS.local.md`):

```sh
sh ../krcompiler/scripts/kr-mirror.sh krdbg
cd ~/mirrors/krdbg
KRG_PREFIX=$HOME/.local bash scripts/build-debugger.sh
ctest --test-dir build/kr232-debugger -R "kr232" --output-on-failure
bash scripts/gate-linux.sh          # bare gate still runs beside the adapter build
```

The family gate (`../kr232/scripts/check-family.ps1`) runs each repository's
gate in dependency order; symbol rows must be green there on Windows and WSL
before the port is called done. The producer-side unit tests run in KRCompiler's
own presets (`KC/docs/kr-build-instructions.md`).

### 9.5 Cross-platform and golden policy

- Fixtures are committed images; CI does not regenerate them. The drift check
  rebuilds them with the compiler the build names and compares bytes, so a
  fixture cannot drift from its source.
- Cross-platform identity requires: forward-slash relative source names,
  identical compile/link argument order, no host paths, no timestamps, and no
  CRLF-dependent label hashing (the kr232 assembler skips `\r` for this reason,
  `KC/src/asm/kr232_assembler.cpp:528-536`).
- Symbol work changes session data, not pixels: only UI changes touch the
  `goldens/*.hash` files. The kr232 variables golden is
  `krdbg_kr232_variables_golden` (`KD/CMakeLists.txt:451-460`); adding a test
  binary source does not shift it.
- Keep the bare gate green separately: `scripts/gate-windows.ps1` /
  `scripts/gate-linux.sh` build with adapters off, so they cannot be evidence
  for an adapter change.

### 9.6 Pass/fail matrix shape

Record one row per requirement × fixture × assertion:

| Req | Fixture | Assertion | Test name | Now | Needs |
|---|---|---|---|---|---|
| R1 | F1 | token-line breakpoint stops on that line/file | `kr232_symbols_cpp_breakpoint_line_equality` | PASS/RED/PENDING | prerequisite phase |

`Now` is measured, never aspirational; a row whose prerequisite has not landed is
`PENDING` and its test is **not committed** (so the gate never implies a
verification that is not running). This is the rule
[symbol-verification-plan.md](symbol-verification-plan.md) §4/§11 uses; copy the
matrix, not just the tests.

### 9.7 Prove the test can fail

For each load-bearing invariant, regenerate one fixture with the defect and
confirm the test goes red before fixing it:

- line truthfulness: regenerate one fixture with the old compiler and watch the
  line-range assertion fail;
- internal linkage: compile a program with a `static` helper and watch a
  breakpoint inside it fail to bind / the frame list omit it;
- frame offsets: flip the adapter's negation and watch locals read zero;
- name contract: change `V.function` to the symbol on a C++ member function and
  watch `locals(0)` empty.

A test that has never failed is a test that has not been shown to test anything.

---

## 10. References

### 10.1 Companion documents

- [debug-symbols-reference.md](debug-symbols-reference.md) — record table,
  minimum set, probe-confirmed KR32 findings (§6.1 name contract, §6.2
  internal-linkage collisions, §6.4 epilogue range).
- [kr232-debug-symbols-gap.md](kr232-debug-symbols-gap.md) — the ranked G1–G12
  gap list with wire-level evidence, the proposed `debug_function` directive and
  the assembler tri-state line state; §7 is the durable summary.
- [register-promoted-locals.md](register-promoted-locals.md) — KR32's home-slot
  mechanism, the `P` format and consumer design, the kr232 dead-store hole.
- [symbol-verification-plan.md](symbol-verification-plan.md) — fixtures F1–F6,
  requirement R1–R15, exact test names, commands, matrix and the three
  highest-risk verifications.
- [debugging-kr232.md](debugging-kr232.md) — how to build and drive the
  adapter-enabled debugger on the kr232 fixtures.
- [working-debugger-plan.md](working-debugger-plan.md) — the krdbg milestone
  plan; §1 lists the wiring defects, §2 the already-working parts.
- [krdbg-newtarget.md](../krdbg-newtarget.md) §4.3–4.6, §9 — the adapter-side
  view of frame offsets, KDBG translation limits, source paths and common
  failures.

### 10.2 Format documents and decisions

- `KC/docs/symbols.md` — bridge record table (`:91-107`), KDBG v1.1 tags and
  sections (`:109-139`), line policy (`:181-193`), variable policy (`:195-199`),
  source-root policy (`:200-205`).
- `KC/docs/decisions/0009-kdbg-v1-freeze.md` — the KDBG v1.1 amendment
  (`:43-64`).
- `KC/docs/decisions/0052-kdbg-variable-scope-ranges.md` — why a live range is
  a `Location` record and why promotion waits for it (`:11-23`).
- `KC/docs/decisions/0061-function-definition-location.md` — the entry label and
  definition-line record (`:31-70`), including the kr232 gap.
- `KC/docs/decisions/0030-debug-display-names.md` — display name vs symbol.
- `KC/docs/optimize.md` — F2 (`:80-84`), E4/E6 and S4 (`:410-419`), the safety
  contract (`:472-492`), the `-O1` debug decision (`:550-570`).
- `KC/docs/plan-o1-debugger.md` — named variables keep slots (`:42-46`), W4/W5.
- `KC/docs/kdbg-v1-1-log.md` — what v1.1 added and what has no producer
  (`:128-145`, `:199-200`).
- `KR32/docs/debug.md` — the reference reader's `P` semantics (`:40-51`).

### 10.3 Code anchors — KRCompiler

| Area | Anchor |
|---|---|
| Bridge append / assemble dispatch | `src/toolchain/toolchain.cpp:169-181`, `:238-322`, `:734-780`; kr232 no-double-append `:295-297` |
| Promotion gate | `src/toolchain/toolchain.cpp:685-686`; refusal `src/ir/passes.cpp:326-341`; `.addr` naming `src/irgen/c_irgen.cpp:116-123`, `:1663-1666` |
| KR32 records | `src/backend/kr32_backend.cpp:809-876` (types), `:979-1105` (emit), `:1062-1081` (globals), `:1285-1297` (S/L), `:1307-1315` (entry line), `:1488-1494` (instruction lines), `:1498-1515` (F/K), `:1516-1586` (V/W), `:1588-1626` (ret/end/exception) |
| KR16 records | `src/backend/kr16_backend.cpp:304-358`, `:440-455`, `:1180-1264`, `:1376-1385` |
| KR216 records | `src/backend/kr216_backend.cpp:457-501`, `:666-677`, `:748-756`, `:1824-1897`, `:2019-2026` |
| KR232 directives | `src/backend/kr232_backend.cpp:1384-1462`, `:1494-1513`, `:1574-1596`, `:1598-1761`, `:1763-1822`, `:1975-1986`; peepholes `:2647-2660` |
| KR232 assembler | `src/asm/kr232_assembler.cpp:128-171` (directives), `:473-508` (symbols), `:510-592` (bridge synthesis) |
| Linker | `src/linker/linker.cpp:123-196` (merge), `:542-563` (symbol table includes locals), `:742-1034` (attach_kdbg), `:758-761` (bare-name map), `:763-782` (header), `:973-985` (orphan lines) |
| KDBG model/encoder | `include/krc/debug/kdbg.h:20-136`, `:141-149` |

### 10.4 Code anchors — krdbg

Line numbers are the HEAD `71e4591` snapshot. The uncommitted consumer change
adds `LocationInfo`, `FunctionInfo::symbol`/`identity()`,
`VariableSlotUnusable`, `location_at()` and PC-aware `make_view`, which shifts
the numbers in `src/kdbg/kdbg.cpp`, `include/krdbg/debug_info.h` and
`src/core/session.cpp`; the names above are the stable anchors.

| Area | Anchor |
|---|---|
| KDBG translation | `src/kdbg/kdbg.cpp:62-218` (sources `:73-80`, types `:82-108`, functions `:110-124`, lines `:126-138`, symbols `:140-148`, W map `:150-154`, variables `:156-187`, globals `:189-217`) |
| Neutral model | `include/krdbg/debug_info.h:18-28` (kinds/location), `:30-36` (flags), `:44-61` (line/function), `:92-119` (variable/model), `:128-152` (views) |
| Session | `src/core/session.cpp:125-141` (breakpoint resolve), `:171-218` (make_view/frame_base), `:362-547` (stepping), `:626-643` (call stack), `:645-685` (locals/globals), `:697-777` (members), `:838-848` (line_at/function_at) |
| Lookup | `src/core/debug_info.cpp:154-255`; `src/core/debug_info_util.h:10-22` |
| Variable reads | `src/core/variables.cpp:16-24` (endianness), `:44-91` (storage), `:152-190` (members) |
| KR32 adapter | `adapters/kr32/kr32_adapter.cpp:76-98` (unwind uses `F.frame`) |
| KR16/kr216 adapters | `adapters/kr16/kr16_adapter.cpp:148-171`, `:675-687`; `adapters/kr216/kr216_adapter.cpp:101-123`, `:371-376` |
| KR232 adapter | `adapters/kr232/kr232_adapter.cpp:298-338` (ENTER unwind), `:408-446` (gpr/fpr groups), `:505-628` (payload discovery and source roots) |
| Tests | `tests/test_kdbg.cpp`, `tests/test_session.cpp:83-294`, `tests/test_kr232_adapter.cpp`, `tests/test_kr232_scenarios.cpp`, `CMakeLists.txt:337-497` |
