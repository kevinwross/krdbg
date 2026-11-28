#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace krdbg {

// Debug-info type, variable and location model. It is neutral: KDBG, the
// vendored bootstrap and a legacy adapter all translate into these types, and
// the surface only ever sees them. Addresses are 64-bit regardless of target.
using TypeId = std::uint32_t;
inline constexpr TypeId kInvalidType = 0xFFFFFFFFU;
inline constexpr std::uint32_t kInvalidFunction = 0xFFFFFFFFU;

enum class TypeKind { Builtin, Pointer, Array, Struct, Union, Enum, Function, Unknown };

enum class LocationKind { None, FrameOffset, Register, Absolute };

struct Location {
    LocationKind kind = LocationKind::None;
    std::int64_t frame_offset = 0;  // FrameOffset
    std::string group;              // Register: group id
    int index = 0;                  // Register: value index
    std::uint64_t address = 0;      // Absolute
};

// A register live range (KDBG `P`/`Location`). A promoted variable keeps its
// declared home in VariableInfo::location and gains one record per half-open
// [start, end) range in which the value lives in `location`. Records are sorted
// by (function, name, start), so the first range containing the PC wins.
struct LocationInfo {
    std::string function;  // function identity, as VariableInfo::function
    std::string name;      // variable name
    Location location;     // Register: group and index
    std::uint64_t start = 0;  // half-open, image address space
    std::uint64_t end = 0;
};

enum VariableFlag : std::uint32_t {
    VariableParam = 1U << 0,
    VariableLocal = 1U << 1,
    VariableGlobal = 1U << 2,
    VariableStatic = 1U << 3,
    // The producer no longer maintains the variable's frame slot at every PC.
    // Outside every LocationInfo range the value is `<optimized out>` rather
    // than the slot's stale bytes.
    VariableSlotUnusable = 1U << 4,
};
using VariableFlags = std::uint32_t;

struct SourceFile {
    std::uint32_t id = 0;
    std::string path;   // path as recorded by the producer
    std::string name;   // display name, usually the file name
};

struct LineInfo {
    std::uint64_t address = 0;
    std::uint32_t source = 0;    // SourceFile::id
    std::uint32_t line = 0;
    std::uint32_t function = kInvalidFunction;  // FunctionInfo index
};

struct FunctionInfo {
    // Source-level name to display (`Class::method`). `symbol` is the identity
    // the payload's variable records name this function by; matching (locals()
    // and frame lookup) uses identity(), never `name`.
    std::string name;
    std::string symbol;
    std::uint64_t begin = 0;
    std::uint64_t end = 0;       // exclusive
    std::uint32_t source = 0;
    std::uint32_t line = 0;
    // ABI frame offset from the frame-pointer register to the saved caller
    // frame (saved FP at +frame, return PC at +frame+pointer size). An adapter
    // that cannot unwind leaves this zero.
    std::uint32_t frame = 0;

    // The function identity a variable record uses. A producer that keeps one
    // name may leave `symbol` empty, in which case identity() returns `name`.
    // KR32/KR16 C++ keep the mangled symbol here while `name` carries the display
    // name.
    [[nodiscard]] std::string_view identity() const {
        return symbol.empty() ? std::string_view(name) : std::string_view(symbol);
    }
};

struct SymbolInfo {
    std::string name;
    std::uint64_t address = 0;
    std::uint32_t size = 0;
    TypeId type = kInvalidType;
};

struct MemberInfo {
    std::string name;
    TypeId type = kInvalidType;
    std::uint32_t offset = 0;
};

struct EnumeratorInfo {
    std::string name;
    std::int64_t value = 0;
};

struct TypeInfo {
    std::string name;
    std::uint32_t size = 0;
    TypeKind kind = TypeKind::Builtin;
    TypeId element = kInvalidType;       // array/pointer target
    std::uint64_t element_count = 0;
    TypeId base = kInvalidType;          // inheritance
    std::vector<MemberInfo> members;
    std::vector<EnumeratorInfo> enumerators;
};

struct VariableInfo {
    std::string function;
    std::string name;
    std::string scope;                    // "" for function scope
    TypeId type = kInvalidType;
    std::uint32_t size = 0;
    Location location;
    std::uint32_t decl_line = 0;
    VariableFlags flags = 0;
};

// The neutral debug model. Vectors that are searched are kept sorted by the
// producer: lines by address, functions by range, locations by (function, name,
// start).
struct DebugInfo {
    std::uint32_t target_id = 0;
    std::uint32_t abi_version = 0;
    std::uint32_t pointer_bits = 32;
    std::uint64_t build_id = 0;
    std::string compiler;

    std::vector<SourceFile> sources;
    std::vector<LineInfo> lines;
    std::vector<FunctionInfo> functions;
    std::vector<SymbolInfo> symbols;
    std::vector<VariableInfo> variables;
    std::vector<LocationInfo> locations;  // register live ranges
    std::vector<TypeInfo> types;
    std::map<std::string, TypeId> type_by_name;
};

// Concatenates a second payload, renumbering source ids and merging type
// tables. KR32's --os mode needs this: the kernel and BIOS images each carry
// their own debug info.
void merge_debug_info(DebugInfo& into, const DebugInfo& other);

// Collapses source records with the same normalized path, keeping the first
// record's order and display name, and renumbers every line/function source
// reference to the surviving id. Linkers that concatenate per-object debug
// payloads legitimately repeat a header or a source file once per object; the
// debugger's source list should show it once.
void deduplicate_sources(DebugInfo& info);

// Resolved views returned by DebugSession. A VariableView carries the location
// actually in effect at the current PC; members() reads through that storage.
struct FrameView {
    int index = 0;
    std::uint64_t pc = 0;
    std::uint64_t frame_base = 0;
    std::string function;
};

struct VariableView {
    std::string name;
    std::string type_name;
    TypeId type = kInvalidType;
    std::string value;
    std::uint32_t size = 0;
    // The location in effect at the current PC: the live-range register when a
    // LocationInfo range covers the PC, otherwise the declared home.
    Location location;
    bool has_children = false;
    // A live range selected a register; `location` is that register and this is
    // its display form ("register: R9"). Empty when no range applied, so a
    // declared register home does not claim to be a live-range value.
    std::string location_text;
    // The value could not be read at this PC: a register-resident variable in a
    // caller frame, or a slot flagged VariableSlotUnusable outside every range.
    bool unavailable = false;
    // The frame base this view's FrameOffset resolves against; members() reads
    // through the location above instead of re-finding the variable.
    std::uint64_t frame_base = 0;
};

struct MemberView {
    std::string name;
    std::string type_name;
    TypeId type = kInvalidType;
    std::string value;
    bool has_children = false;
    std::vector<std::uint8_t> storage;  // this member's bytes, for nested expansion
};

} // namespace krdbg
