#include "krdbg/debug_info.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "debug_info_util.h"

namespace krdbg {
namespace {

std::string normalize_path(std::string_view path) {
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::size_t function_index(const DebugInfo& info, const FunctionInfo* function) {
    if (function == nullptr || info.functions.empty()) {
        return info.functions.size();
    }
    return static_cast<std::size_t>(function - info.functions.data());
}

} // namespace

void merge_debug_info(DebugInfo& into, const DebugInfo& other) {
    if (&into == &other) {
        return;
    }

    const auto type_offset = static_cast<std::uint32_t>(into.types.size());
    const auto function_offset = static_cast<std::uint32_t>(into.functions.size());

    // Renumber both payloads to one contiguous 0..N-1 source namespace so no
    // id from the second payload can collide with the first.
    std::map<std::uint32_t, std::uint32_t> into_sources;
    for (std::size_t index = 0; index < into.sources.size(); ++index) {
        into_sources[into.sources[index].id] = static_cast<std::uint32_t>(index);
        into.sources[index].id = static_cast<std::uint32_t>(index);
    }
    std::map<std::uint32_t, std::uint32_t> other_sources;
    const std::size_t into_source_count = into.sources.size();
    for (std::size_t index = 0; index < other.sources.size(); ++index) {
        const SourceFile& source = other.sources[index];
        const auto new_id = static_cast<std::uint32_t>(into_source_count + index);
        other_sources[source.id] = new_id;
        SourceFile copy = source;
        copy.id = new_id;
        into.sources.push_back(std::move(copy));
    }

    const auto remap_source = [](const std::map<std::uint32_t, std::uint32_t>& map,
                                 std::uint32_t id) {
        const auto found = map.find(id);
        return found == map.end() ? id : found->second;
    };
    for (FunctionInfo& function : into.functions) {
        function.source = remap_source(into_sources, function.source);
    }
    for (LineInfo& line : into.lines) {
        line.source = remap_source(into_sources, line.source);
    }

    // Functions are sorted by begin and lines carry function indexes, so merge
    // both tables together and remap every line index once.
    std::vector<FunctionInfo> combined = into.functions;
    for (FunctionInfo function : other.functions) {
        function.source = remap_source(other_sources, function.source);
        combined.push_back(std::move(function));
    }

    std::vector<std::uint32_t> order(combined.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(), [&combined](std::uint32_t left,
                                                             std::uint32_t right) {
        return combined[left].begin < combined[right].begin;
    });
    std::vector<std::uint32_t> remap(combined.size());
    std::vector<FunctionInfo> sorted;
    sorted.reserve(combined.size());
    for (std::size_t new_index = 0; new_index < order.size(); ++new_index) {
        remap[order[new_index]] = static_cast<std::uint32_t>(new_index);
        sorted.push_back(std::move(combined[order[new_index]]));
    }
    into.functions = std::move(sorted);

    for (LineInfo& line : into.lines) {
        if (line.function != kInvalidFunction && line.function < remap.size()) {
            line.function = remap[line.function];
        }
    }
    for (LineInfo line : other.lines) {
        line.source = remap_source(other_sources, line.source);
        if (line.function != kInvalidFunction) {
            const std::size_t index = function_offset + line.function;
            if (index < remap.size()) {
                line.function = remap[index];
            } else {
                line.function = kInvalidFunction;
            }
        }
        into.lines.push_back(std::move(line));
    }
    std::stable_sort(into.lines.begin(), into.lines.end(),
                     [](const LineInfo& left, const LineInfo& right) {
                         return left.address < right.address;
                     });

    for (TypeInfo type : other.types) {
        if (type.element != kInvalidType) {
            type.element += type_offset;
        }
        if (type.base != kInvalidType) {
            type.base += type_offset;
        }
        for (MemberInfo& member : type.members) {
            if (member.type != kInvalidType) {
                member.type += type_offset;
            }
        }
        into.types.push_back(std::move(type));
    }
    // The primary payload wins a type-name collision; a later duplicate is
    // reachable by id but not by name.
    for (const auto& [name, id] : other.type_by_name) {
        const TypeId remapped = id == kInvalidType ? kInvalidType : id + type_offset;
        into.type_by_name.emplace(name, remapped);
    }

    for (SymbolInfo symbol : other.symbols) {
        if (symbol.type != kInvalidType) {
            symbol.type += type_offset;
        }
        into.symbols.push_back(std::move(symbol));
    }

    for (VariableInfo variable : other.variables) {
        if (variable.type != kInvalidType) {
            variable.type += type_offset;
        }
        into.variables.push_back(std::move(variable));
    }

    // Location records name their function by identity string, not by index, so
    // the function renumbering above does not touch them. Sort once so the
    // merged list keeps the (function, name, start) order location_at() needs.
    for (LocationInfo location : other.locations) {
        into.locations.push_back(std::move(location));
    }
    std::stable_sort(into.locations.begin(), into.locations.end(),
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

namespace detail {

const FunctionInfo* find_function(const DebugInfo& info, std::uint64_t pc) {
    if (info.functions.empty()) {
        return nullptr;
    }
    const auto it = std::upper_bound(
        info.functions.begin(), info.functions.end(), pc,
        [](std::uint64_t value, const FunctionInfo& function) {
            return value < function.begin;
        });
    for (auto candidate = it; candidate != info.functions.begin();) {
        --candidate;
        if (pc < candidate->end) {
            return &*candidate;
        }
        if (candidate->begin == 0) {
            break;
        }
    }
    return nullptr;
}

const LocationInfo* location_at(const DebugInfo& info, std::string_view function,
                                std::string_view name, std::uint64_t pc) {
    for (const LocationInfo& location : info.locations) {
        if (location.function == function && location.name == name &&
            location.start <= pc && pc < location.end) {
            return &location;
        }
    }
    return nullptr;
}

const LineInfo* find_line(const DebugInfo& info, std::uint64_t pc) {
    if (info.lines.empty()) {
        return nullptr;
    }

    const FunctionInfo* function = find_function(info, pc);
    if (function == nullptr) {
        const auto it = std::upper_bound(
            info.lines.begin(), info.lines.end(), pc,
            [](std::uint64_t value, const LineInfo& line) {
                return value < line.address;
            });
        if (it == info.lines.begin()) {
            return nullptr;
        }
        return &*(it - 1);
    }

    const std::size_t index = function_index(info, function);
    if (function->end <= function->begin) {
        return nullptr;
    }
    const auto first = std::lower_bound(
        info.lines.begin(), info.lines.end(), function->begin,
        [](const LineInfo& line, std::uint64_t value) {
            return line.address < value;
        });
    const auto last = std::upper_bound(
        info.lines.begin(), info.lines.end(), function->end - 1,
        [](std::uint64_t value, const LineInfo& line) {
            return value < line.address;
        });
    if (first >= last) {
        return nullptr;
    }
    auto it = std::upper_bound(
        first, last, pc, [](std::uint64_t value, const LineInfo& line) {
            return value < line.address;
        });
    if (it == first) {
        return nullptr;
    }
    --it;
    if (it->function != kInvalidFunction && it->function != index) {
        while (it != first) {
            --it;
            if (it->function == index) {
                return &*it;
            }
        }
        return nullptr;
    }
    return &*it;
}

const SourceFile* find_source(const DebugInfo& info, std::string_view path) {
    const std::string wanted = normalize_path(path);
    if (wanted.empty()) {
        return nullptr;
    }
    for (const SourceFile& source : info.sources) {
        if (normalize_path(source.path) == wanted) {
            return &source;
        }
    }
    for (const SourceFile& source : info.sources) {
        if (normalize_path(source.name) == wanted) {
            return &source;
        }
    }
    for (const SourceFile& source : info.sources) {
        const std::string candidate = normalize_path(source.path);
        if (candidate.size() > wanted.size() &&
            candidate.compare(candidate.size() - wanted.size(), wanted.size(), wanted) ==
                0 &&
            candidate[candidate.size() - wanted.size() - 1] == '/') {
            return &source;
        }
    }
    return nullptr;
}

const TypeInfo* find_type(const DebugInfo& info, TypeId id) {
    if (id == kInvalidType || id >= info.types.size()) {
        return nullptr;
    }
    return &info.types[id];
}

const TypeInfo* find_type(const DebugInfo& info, std::string_view name) {
    const auto it = info.type_by_name.find(std::string(name));
    return it == info.type_by_name.end() ? nullptr : find_type(info, it->second);
}

} // namespace detail
} // namespace krdbg
