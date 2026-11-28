#include "krdbg/kdbg.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "krc/debug/kdbg.h"

namespace krdbg::kdbg {
namespace {

std::string file_name(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

TypeKind map_kind(std::string_view kind) {
    if (kind == "struct") {
        return TypeKind::Struct;
    }
    if (kind == "union") {
        return TypeKind::Union;
    }
    if (kind == "enum") {
        return TypeKind::Enum;
    }
    if (kind == "pointer") {
        return TypeKind::Pointer;
    }
    if (kind == "array") {
        return TypeKind::Array;
    }
    if (kind == "builtin") {
        return TypeKind::Builtin;
    }
    return TypeKind::Unknown;
}

std::uint32_t function_at(const DebugInfo& info, std::uint64_t address) {
    std::uint32_t result = kInvalidFunction;
    std::uint64_t best_begin = 0;
    for (std::size_t index = 0; index < info.functions.size(); ++index) {
        const FunctionInfo& function = info.functions[index];
        if (function.begin <= address && address < function.end &&
            (result == kInvalidFunction || function.begin >= best_begin)) {
            result = static_cast<std::uint32_t>(index);
            best_begin = function.begin;
        }
    }
    return result;
}

TypeId type_id(const DebugInfo& info, const std::string& name) {
    const auto found = info.type_by_name.find(name);
    return found == info.type_by_name.end() ? kInvalidType : found->second;
}

// KDBG keeps one name per function: bridge `F` carries a display name and the
// linker resolves the mangled symbol (F's start label) to an address, dropping
// the string. A producer that keeps a distinct symbol also emits the
// function-scope `K` record, whose `function` is that symbol and whose range
// starts at the function's start label. Use it as the identity when the
// payload's variable records name the function by it (KR32/KR16 C++, and a
// kr232 producer that names V/W by symbol); leave name == symbol when they use
// the display name (kr216).
void assign_function_symbols(const krc::debug::KdbgImage& image, DebugInfo& out) {
    std::set<std::string> variable_functions;
    for (const krc::debug::KdbgVariable& variable : image.variables) {
        variable_functions.insert(variable.function);
    }
    for (const krc::debug::KdbgVarInfo& info : image.variable_info) {
        variable_functions.insert(info.function);
    }
    for (FunctionInfo& function : out.functions) {
        function.symbol = function.name;
        for (const krc::debug::KdbgScope& scope : image.scopes) {
            if (!scope.parent.empty() || scope.start != function.begin ||
                scope.function.empty()) {
                continue;
            }
            if (variable_functions.count(scope.function) == 0) {
                continue;
            }
            function.symbol = scope.function;
            break;
        }
    }
}

void translate(const krc::debug::KdbgImage& image, DebugInfo& out) {
    out = DebugInfo{};
    out.target_id = image.target_id;
    out.abi_version = image.abi_version;
    out.pointer_bits = image.pointer_width == 0 ? 32U
                                                : static_cast<std::uint32_t>(
                                                      image.pointer_width) *
                                                      8U;
    out.build_id = image.build_id;
    out.compiler = image.compiler;

    out.sources.reserve(image.sources.size());
    for (const krc::debug::KdbgSource& source : image.sources) {
        SourceFile file;
        file.id = source.id;
        file.path = source.path;
        file.name = file_name(source.path);
        out.sources.push_back(std::move(file));
    }

    out.types.reserve(image.types.size());
    for (const krc::debug::KdbgType& type : image.types) {
        TypeInfo info;
        info.name = type.name;
        info.size = type.size;
        info.kind = map_kind(type.kind);
        out.types.push_back(std::move(info));
    }
    for (std::size_t index = 0; index < out.types.size(); ++index) {
        out.type_by_name.emplace(out.types[index].name,
                                 static_cast<TypeId>(index));
    }
    for (std::size_t index = 0; index < image.types.size(); ++index) {
        const krc::debug::KdbgType& type = image.types[index];
        TypeInfo& translated = out.types[index];
        translated.base = type_id(out, type.base);
        translated.element = type_id(
            out, type.element.empty() ? type.target : type.element);
        translated.element_count = type.element_count;
        for (const krc::debug::KdbgMember& member : type.members) {
            MemberInfo info;
            info.name = member.name;
            info.offset = member.offset;
            info.type = type_id(out, member.type);
            translated.members.push_back(std::move(info));
        }
    }

    out.functions.reserve(image.functions.size());
    for (const krc::debug::KdbgFunction& function : image.functions) {
        FunctionInfo info;
        info.name = function.name;
        info.begin = function.start;
        info.end = function.end;
        info.source = 0;
        info.line = 0;
        info.frame = function.frame;
        out.functions.push_back(std::move(info));
    }
    std::stable_sort(out.functions.begin(), out.functions.end(),
                     [](const FunctionInfo& left, const FunctionInfo& right) {
                         return left.begin < right.begin;
                     });

    out.lines.reserve(image.lines.size());
    for (const krc::debug::KdbgLine& line : image.lines) {
        LineInfo info;
        info.address = line.address;
        info.source = line.source;
        info.line = line.line;
        info.function = function_at(out, line.address);
        out.lines.push_back(info);
    }
    std::stable_sort(out.lines.begin(), out.lines.end(),
                     [](const LineInfo& left, const LineInfo& right) {
                         return left.address < right.address;
                     });

    out.symbols.reserve(image.symbols.size());
    for (const krc::debug::KdbgSymbol& symbol : image.symbols) {
        SymbolInfo info;
        info.name = symbol.name;
        info.address = symbol.address;
        info.size = static_cast<std::uint32_t>(symbol.size);
        info.type = type_id(out, symbol.type);
        out.symbols.push_back(std::move(info));
    }

    std::map<std::string, const krc::debug::KdbgVarInfo*> variable_info;
    for (const krc::debug::KdbgVarInfo& metadata : image.variable_info) {
        variable_info.emplace(metadata.function + "\n" + metadata.name,
                              &metadata);
    }

    out.variables.reserve(image.variables.size() + image.symbols.size());
    for (const krc::debug::KdbgVariable& variable : image.variables) {
        VariableInfo info;
        info.function = variable.function;
        info.name = variable.name;
        info.type = type_id(out, variable.type);
        info.size = variable.size;
        // The payload's offset is a signed 32-bit frame displacement, and the
        // debugger adds it to the frame base. Widening it unsigned turns -4 into
        // 4294967292, which reads a local four gigabytes away from where it lives.
        info.location = Location{LocationKind::FrameOffset,
                                 static_cast<std::int64_t>(static_cast<std::int32_t>(variable.offset)),
                                 {}, 0, 0};
        info.flags = variable.function.empty() ? VariableGlobal : VariableLocal;
        const auto metadata =
            variable_info.find(variable.function + "\n" + variable.name);
        if (metadata != variable_info.end()) {
            info.scope = metadata->second->scope;
            switch (metadata->second->kind) {
            case krc::debug::KdbgVariableKind::Parameter:
                info.flags = VariableParam;
                break;
            case krc::debug::KdbgVariableKind::Static:
                info.flags = VariableStatic;
                break;
            case krc::debug::KdbgVariableKind::Local:
                info.flags = VariableLocal;
                break;
            }
            // W flag bit 2 (0x4) marks a home slot the producer no longer
            // maintains. The other bits (address-taken, read-only) have no
            // consumer here; an out-of-range read must not use this slot.
            if ((metadata->second->flags & 0x4U) != 0U) {
                info.flags |= krdbg::VariableSlotUnusable;
            }
        }
        out.variables.push_back(std::move(info));
    }

    // KDBG records data symbols separately from locals. Expose the data ones as
    // absolute globals; skip function symbols and anything already declared.
    for (const krc::debug::KdbgSymbol& symbol : image.symbols) {
        if (symbol.size == 0) {
            continue;
        }
        const bool is_function =
            std::any_of(out.functions.begin(), out.functions.end(),
                        [&symbol](const FunctionInfo& function) {
                            return function.name == symbol.name;
                        });
        const bool already_declared =
            std::any_of(out.variables.begin(), out.variables.end(),
                        [&symbol](const VariableInfo& variable) {
                            return variable.function.empty() &&
                                   variable.name == symbol.name;
                        });
        if (is_function || already_declared) {
            continue;
        }
        VariableInfo info;
        info.name = symbol.name;
        info.type = type_id(out, symbol.type);
        info.size = static_cast<std::uint32_t>(symbol.size);
        info.location =
            Location{LocationKind::Absolute, 0, {}, 0, symbol.address};
        info.flags = VariableGlobal;
        out.variables.push_back(std::move(info));
    }

    // The variable identity is settled before the function symbols derive from
    // it. A v1 payload has no K records and no distinct symbols, so both names
    // stay the F name and behaviour is unchanged.
    assign_function_symbols(image, out);

    // A `P`/Location record becomes a register live range. The fp flag selects
    // the floating-point register file; the index is the register number. A v1
    // payload has no Location records, so this leaves the list empty.
    out.locations.reserve(image.locations.size());
    for (const krc::debug::KdbgLocation& location : image.locations) {
        LocationInfo info;
        info.function = location.function;
        info.name = location.name;
        info.location = Location{LocationKind::Register, 0,
                                 location.fp ? "fpr" : "gpr",
                                 static_cast<int>(location.reg), 0};
        info.start = location.start;
        info.end = location.end;
        out.locations.push_back(std::move(info));
    }
    // Deterministic search order when a producer emits overlapping ranges: the
    // earliest start wins, then the payload order.
    std::stable_sort(out.locations.begin(), out.locations.end(),
                     [](const LocationInfo& left, const LocationInfo& right) {
                         if (left.function != right.function) {
                             return left.function < right.function;
                         }
                         if (left.name != right.name) {
                             return left.name < right.name;
                         }
                         return left.start < right.start;
                     });
}

} // namespace

bool read_payload(std::span<const std::uint8_t> payload, DebugInfo& out,
                  std::string& error) {
    krc::debug::KdbgImage image;
    if (!krc::debug::decode_kdbg(payload, image, error)) {
        return false;
    }
    translate(image, out);
    error.clear();
    return true;
}

bool read_section(std::span<const std::uint8_t> container, DebugInfo& out,
                  std::string& error) {
    krc::debug::KdbgImage image;
    if (!krc::debug::decode_kdbg_image(container, image, error)) {
        return false;
    }
    translate(image, out);
    error.clear();
    return true;
}

} // namespace krdbg::kdbg
