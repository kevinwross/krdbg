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

enum VariableFlag : std::uint32_t {
    VariableParam = 1U << 0,
    VariableLocal = 1U << 1,
    VariableGlobal = 1U << 2,
    VariableStatic = 1U << 3,
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
    std::string name;
    std::uint64_t begin = 0;
    std::uint64_t end = 0;       // exclusive
    std::uint32_t source = 0;
    std::uint32_t line = 0;
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
// producer: lines by address, functions by range.
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
    std::vector<TypeInfo> types;
    std::map<std::string, TypeId> type_by_name;
};

// Concatenates a second payload, renumbering source ids and merging type
// tables. KR32's --os mode needs this: the kernel and BIOS images each carry
// their own debug info.
void merge_debug_info(DebugInfo& into, const DebugInfo& other);

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
    Location location;
    bool has_children = false;
};

struct MemberView {
    std::string name;
    std::string type_name;
    TypeId type = kInvalidType;
    std::string value;
    bool has_children = false;
};

} // namespace krdbg
