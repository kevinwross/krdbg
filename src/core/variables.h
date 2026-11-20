#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "krdbg/debug_info.h"
#include "krdbg/target.h"

namespace krdbg::detail {

// Reads the bytes that back a variable at the current stop. A FrameOffset is
// resolved against frame_base, an Absolute location against its address, and a
// Register against the target's value width and byte order. Returns false when
// the location is None, the size is unusable, or the memory is unreadable.
bool read_variable_storage(const IDebugTarget& target, std::uint64_t frame_base,
                           const VariableInfo& variable,
                           std::vector<std::uint8_t>& out);

// Formats one scalar. Pointers are hex, float/double use their bit pattern,
// enums prefer a matching enumerator, and integers are signed unless the type
// name says otherwise.
std::string format_scalar(const TypeInfo* type, std::span<const std::uint8_t> bytes,
                          bool little_endian);

// Expands one aggregate level from `storage`. A struct or union member reports
// has_children and "{...}"; an array reports "[N]". A member that does not fit
// in the storage reports "?".
std::vector<MemberView> expand_members(const DebugInfo& info, TypeId type_id,
                                       std::span<const std::uint8_t> storage,
                                       bool little_endian);

} // namespace krdbg::detail
