#pragma once

#include <cstdint>
#include <string_view>

#include "krdbg/debug_info.h"

namespace krdbg::detail {

// The innermost function whose [begin, end) range contains pc, or nullptr.
// `info.functions` is sorted by begin; overlapping ranges resolve to the one
// with the latest begin.
const FunctionInfo* find_function(const DebugInfo& info, std::uint64_t pc);

// The last line at or before pc, clamped to the enclosing function. Returns
// nullptr when pc is before the first line or before the enclosing function's
// first line.
const LineInfo* find_line(const DebugInfo& info, std::uint64_t pc);

// The first LocationInfo for (function, name) whose half-open [start, end)
// contains pc, or nullptr. `function` is the identity variable records use
// (VariableInfo::function), not the display name. `locations` is sorted by
// (function, name, start), so overlapping ranges resolve deterministically to
// the earliest start.
const LocationInfo* location_at(const DebugInfo& info, std::string_view function,
                                std::string_view name, std::uint64_t pc);

// Exact path, then file name, then a path suffix after '/' or '\'. Separators
// are normalized before comparison.
const SourceFile* find_source(const DebugInfo& info, std::string_view path);

const TypeInfo* find_type(const DebugInfo& info, TypeId id);
const TypeInfo* find_type(const DebugInfo& info, std::string_view name);

} // namespace krdbg::detail
