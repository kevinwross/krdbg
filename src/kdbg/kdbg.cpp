#include "krdbg/kdbg.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
        for (const krc::debug::KdbgMember& member : image.types[index].members) {
            MemberInfo info;
            info.name = member.name;
            info.offset = member.offset;
            info.type = type_id(out, member.type);
            out.types[index].members.push_back(std::move(info));
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
        info.size = symbol.size;
        info.type = type_id(out, symbol.type);
        out.symbols.push_back(std::move(info));
    }

    out.variables.reserve(image.variables.size());
    for (const krc::debug::KdbgVariable& variable : image.variables) {
        VariableInfo info;
        info.function = variable.function;
        info.name = variable.name;
        info.type = type_id(out, variable.type);
        info.size = variable.size;
        info.location = Location{LocationKind::FrameOffset,
                                 static_cast<std::int64_t>(variable.offset), {}, 0, 0};
        info.flags = variable.function.empty() ? VariableGlobal : VariableLocal;
        out.variables.push_back(std::move(info));
    }
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
