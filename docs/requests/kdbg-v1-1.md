# Request plan — KRCompiler: KDBG v1.1

**Status:** APPROVED 2026-11-20T11:11:16-08:00 by kevinwross.
**Branch:** `kdbg-v1-1`, from `adopt-krc` at `0951c6e`. Do not commit to `master`
and do not move `adopt-krc`; consumers pin SHAs on it.
**Log:** `docs/kdbg-v1-1-log.md`, append-only, every entry headed by the exact
`iso` from `local-datetime`.
**Target repository:** `C:\Users\kevin\source\repos\KRCompiler`. Base the work on
`adopt-krc` at `0951c6e`, which carries stage A of the toolchain adoption; the
same tree is not on `master`. Confirm the merge/push state before branching.
**Proposed:** 2026-11-20T11:09:49-08:00
**Requested by:** kevinwross
**Requirement source:** `krdbg/PLAN.md` §5.2 (gaps against the KR32 surface) and
§5.3 (the approved additive shape). This plan turns §5.3 into KRCompiler work.
**Consumers:** krdbg's `krdbg_kdbg` (vendored today, `krc::debug` installed),
kr16's new `.kdbg` reader, KR32's `debug::parse_info` and DAP, and
`krc --dump-symbols`.

---

## 1. Why this change

KDBG v1 carries sources, lines, functions, symbols, variables, types and build
metadata. It cannot express six things the KR32 surface already shows:

| Missing | Consequence today |
|---|---|
| Register-held locals (locations) | the compiler refuses to promote a source variable, because a frame offset would then point at a dead slot (`docs/symbols.md`), so `-O1` is held back and krdbg cannot show `register: R9` |
| Parameter/local/static flags and scopes | shadowed locals collide by `function + name`; params are indistinguishable |
| Class inheritance | a derived class with no own members shows nothing in a viewer that reads only its own record |
| Array element type and count | arrays cannot be paged or expanded correctly |
| 64-bit addresses | a `kr64` target cannot be represented; every address field is `u32` |
| Real target identity | `target_id` is written as `1` regardless of target |

The bridge is closer than it looks. KR32's own dialect already carries `P`
(register location with a live range) and `C` (class, size, base), and its parser
`src/debug/debug.cpp:138-244` is a chain of `if (kind == …)` with no trailing
error, so an unknown record is skipped. krc's KR32 backend does not emit `P`, and
emits `C` with an empty base (`src/backend/kr32_backend.cpp:807-810`). The gaps
are in what krc emits and in what KDBG can encode, not in a format that must be
invented from nothing.

## 2. Scope

**In scope**

1. Bridge records for locations, scope and variable flags, inheritance, array
   element type/count, type references and 64-bit addresses, additive to the
   existing `S/L/F/V/G/T/C/M` set.
2. KDBG encoding for those records: new tags inside existing sections and new
   sections where the shape does not fit, with minor version `1.1`.
3. A real `target_id` from `kROTargetId` (`src/linker/kro_object.cpp:131`) for
   `.k32`, `.kex` and future 64-bit images.
4. Reader and dumper support in `krc::debug`, including text and JSON output.
5. Fixtures and tests: round-trip, parity with `krc --dump-symbols`, corrupt and
   truncated input, a v1 reader on a v1.1 payload, and KR32's parser on a
   v1.1-built `.k32`.
6. Documentation: `docs/symbols.md`, ADR 0009 (the minority bump and the new
   tags), ADR 0052 (live ranges), and `docs/toolchain-embedding.md` if the
   reader's fields change.

**Out of scope**

- Enabling slot reuse or promotion in the allocator. This plan makes the format
  able to describe a promoted variable; turning it on is a separate change under
  ADR 0052, with its own tests.
- Updating KR32's GUI or DAP, kr16's reader, or krdbg. They keep working because
  the changes are additive; they gain the fields when their own plans say so.
- KDBG v2, changing the bridge's stability status, or replacing the textual
  bridge with direct emission.

## 3. Design

**The bridge stays additive.** New record kinds are new letters; no existing
record changes shape or field order. KR32's parser ignores what it does not know,
so its GUI and DAP keep working against a v1.1-built image. A bridge consumer
that wants the new facts reads the new letters.

**KDBG stays additive and versioned.** New tags go into the existing sections
where the shape fits, and new sections carry what does not: locations, scopes and
64-bit address variants. The major version remains `1`; the minor becomes `1`.
Readers skip unknown tags and unknown sections by construction
(`docs/symbols.md:36`), so a v1 reader accepts a v1.1 payload and reports what it
understands. A reader that *requires* a record it does not know reports "newer
KDBG" rather than guessing.

**Addresses.** v1 records keep their 32-bit fields. 64-bit variants are new tags
with `u64` fields, emitted when the target's pointer width is 8. `target_id` is
written from `kROTargetId`: `1` kr32, `2` kr16, `3` kr64.

**Live ranges.** A location record carries `start` and `end`, so a variable's
validity is expressed where the frame offset cannot. Whether the allocator then
reuses slots is ADR 0052's separate decision.

**Not settled here:** the exact bridge spellings and KDBG tag numbers. They are
the first review point of K1, and they land in `docs/symbols.md` before the
encoder does.

## 4. Work items

| ID | Change | Done when |
|---|---|---|
| K1 | Bridge records for register locations (reuse `P`), scope and variable flags, array element/count, type refs; settle the exact spellings in `docs/symbols.md` first | a hand-built fixture with a location record decodes through `krc::debug` and prints in both dumper forms, and a v1-built `.k32` still parses in KR32's `debug` path; the producer side lands with the ADR 0052 promotion change |
| K2 | KDBG encoding: `Location`, `VarInfo`, `Scope` tags and `.kdbg_loc`/`.kdbg_scope` sections; minor `1.1` | decode returns the fields; a v1 reader (a fixture built from the previous decoder) skips them without error |
| K3 | `Base` tag: the backend emits the base type name it has, or the IR gains the field if it does not | a derived class with no own members expands through its base in `krc --dump-symbols` |
| K4 | `ArrayType` and `TypeRef` tags: element type and count, no type-string parsing | an array decodes with its element type and count; a pointer's target is a type reference |
| K5 | 64-bit address variants for line, function, symbol and location start/end | a `kr64` fixture decodes with `u64` addresses; `pointer width` in the header is `8` |
| K6 | Real `target_id` from `kROTargetId` | a KR32 image says `1`, a KR16 image `2`, and the dumper prints it |
| K7 | Reader and dumper: text and stable JSON gain the fields; unknown required records report "newer KDBG" | the JSON schema is documented and a fixture round-trips through text and JSON |
| K8 | Fixtures and tests: round-trip, parity, corrupt/truncated/CRC, v1-reader compatibility, KR32 parser | the suite covers each failure path, and a v1.1-built `.k32` loads in KR32's `debug` command |
| K9 | Documentation: `docs/symbols.md`, ADR 0009 amendment, ADR 0052 update, embedding notes | the format document is true of the encoder that ships |

## 5. Gate

The repository's own presets, from `.github/workflows/ci.yml`. Re-check at
execution; do not substitute a bare `cmake -S . -B build`, which drops `-Werror`.

```pwsh
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
ctest --preset windows-msvc-debug --output-on-failure
```

```sh
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --parallel
ctest --preset linux-gcc-debug --output-on-failure
```

Plus the compatibility rows: a v1 reader fixture on a v1.1 payload, KR32's parser
on a v1.1-built `.k32`, and `krc --dump-symbols` in both forms. Every check must
be run and its real output recorded; nothing is claimed from this plan.

## 6. Risks

| Risk | Why it bites | Mitigation |
|---|---|---|
| A bridge shape change breaks KR32's parser | its IPC is positional, one line per record | additive letters only; K8 runs KR32's `debug` on a v1.1 image |
| A resized field breaks a v1 reader | old readers read fixed-width fields | 64-bit forms are new tags, never resized v1 records |
| `target_id` surprises a consumer | something may assume `1` | the header field already exists; K6 runs the dumper and the KR32 parser against a KR16 image |
| Promotion stays off | the format can describe a location but the compiler still refuses to create one | K1 emits locations the backend already knows; enabling promotion is ADR 0052, separately |
| Scope/live-range semantics drift | start/end labels vs addresses, and reload/restart | one rule, documented and tested in K2 |
| Consumers lag | the fields exist but no reader shows them | accepted: each consumer's own plan picks them up; krdbg's M3/M4 are first |

## 7. Stop conditions

- The v1 reader test cannot be made to pass without changing a v1 record's shape:
  stop — that is a v2 decision, not v1.1.
- KR32's parser rejects a v1.1-built `.k32` for any reason: stop and report; the
  additivity promise is broken.
- The IR has no base-type information and adding it changes layout or ABI: stop
  and return to the approver before touching codegen.

## 8. Decisions

Resolved with the approver on 2026-11-20T11:11:16-08:00; these replace the four
questions the plan was proposed with.

1. **The bridge stays and is extended.** New record letters only; no direct
   emission from the backends.
2. **The 64-bit encoding lands now.** K5 ships as format and tests, with a
   fixture; it does not wait for a `kr64` producer.
3. **krdbg consumes first.** Its M3/M4 are the reason for the work; KR32's DAP
   and kr16's reader follow in their own plans.
4. **Minor bump only.** Major `1`, minor `1`; no known consumer pins the exact
   minor.
5. **Branch `kdbg-v1-1` from `0951c6e`,** not more commits on `adopt-krc`, whose
   SHAs the consumers pin.
6. **K1's producer is deferred.** Decided 2026-11-20T12:29:53-08:00. Promotion
   stays off, so no compiler path emits a location record yet. K1 delivers the
   format, the decoder and a hand-built fixture, and its done-when is amended to
   say so; the producer arrives with the ADR 0052 promotion change, which this
   plan already excludes.

## 9. Approval

Approved by kevinwross on 2026-11-20T11:11:16-08:00.

Approving this file authorises, in KRCompiler only: the new bridge records, the
new KDBG tags and sections, the minor version bump, the real `target_id`, the
reader and dumper fields, the fixtures and tests, and the documentation changes.
It does not authorise enabling promotion or slot reuse, changing `kr32`, `kr16`
or `krdbg`, or starting KDBG v2.
