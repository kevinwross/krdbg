#include "variables.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "debug_info_util.h"

namespace krdbg::detail {
namespace {

std::uint64_t load_bits(std::span<const std::uint8_t> bytes, bool little_endian) {
    std::uint64_t value = 0;
    const std::size_t count = std::min<std::size_t>(bytes.size(), 8);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t source = little_endian ? index : bytes.size() - 1 - index;
        value |= static_cast<std::uint64_t>(bytes[source]) << (8 * index);
    }
    return value;
}

bool is_unsigned_name(const std::string& name) {
    return name.rfind("unsigned", 0) == 0 || name.rfind("uint", 0) == 0 ||
           name == "size_t" || name == "char8_t";
}

std::int64_t sign_extend(std::uint64_t value, std::size_t bytes) {
    if (bytes == 0 || bytes >= 8) {
        return static_cast<std::int64_t>(value);
    }
    const std::uint64_t sign = 1ULL << (8 * bytes - 1);
    if ((value & sign) != 0U) {
        value |= ~0ULL << (8 * bytes);
    }
    return static_cast<std::int64_t>(value);
}

} // namespace

bool read_variable_storage(const IDebugTarget& target, std::uint64_t frame_base,
                           const VariableInfo& variable,
                           std::vector<std::uint8_t>& out) {
    out.clear();
    if (variable.size == 0 && variable.location.kind != LocationKind::Register) {
        return false;
    }

    switch (variable.location.kind) {
    case LocationKind::Register: {
        ScalarValue value;
        if (!target.read_value(variable.location.group, variable.location.index, value)) {
            return false;
        }
        const std::size_t bytes = value.width / 8U;
        if (bytes == 0 || bytes > sizeof(std::uint64_t)) {
            return false;
        }
        std::size_t count = bytes;
        if (variable.size != 0) {
            if (variable.size > bytes) {
                return false;
            }
            count = variable.size;
        }
        out.resize(count);
        const bool little_endian =
            target.layout(target.current_context()).little_endian;
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t shift = little_endian ? index : count - 1 - index;
            out[index] = static_cast<std::uint8_t>((value.bits >> (8 * shift)) & 0xFFU);
        }
        return true;
    }
    case LocationKind::FrameOffset: {
        const auto address = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(frame_base) + variable.location.frame_offset);
        out.resize(variable.size);
        return target.read_memory(address, out);
    }
    case LocationKind::Absolute:
        out.resize(variable.size);
        return target.read_memory(variable.location.address, out);
    case LocationKind::None:
        return false;
    }
    return false;
}

std::string format_scalar(const TypeInfo* type, std::span<const std::uint8_t> bytes,
                          bool little_endian) {
    if (bytes.empty()) {
        return "?";
    }
    if (type != nullptr && type->kind == TypeKind::Pointer) {
        char buffer[32] = {};
        const std::uint64_t value = load_bits(bytes, little_endian);
        std::snprintf(buffer, sizeof(buffer), "0x%0*llX",
                      static_cast<int>(bytes.size() * 2),
                      static_cast<unsigned long long>(value));
        return buffer;
    }
    if (type != nullptr && type->kind == TypeKind::Enum) {
        const std::uint64_t value = load_bits(bytes, little_endian);
        for (const EnumeratorInfo& enumerator : type->enumerators) {
            if (static_cast<std::uint64_t>(enumerator.value) == value) {
                return enumerator.name;
            }
        }
    }
    const std::string name = type != nullptr ? type->name : std::string{};
    if (name == "float" && bytes.size() == 4) {
        const float value = std::bit_cast<float>(static_cast<std::uint32_t>(
            load_bits(bytes, little_endian)));
        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
        return buffer;
    }
    if (name == "double" && bytes.size() == 8) {
        const double value = std::bit_cast<double>(load_bits(bytes, little_endian));
        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "%g", value);
        return buffer;
    }
    if (name == "bool") {
        return load_bits(bytes, little_endian) != 0 ? "true" : "false";
    }
    if (name == "char") {
        const auto value =
            static_cast<unsigned char>(load_bits(bytes, little_endian) & 0xFFU);
        if (value >= 32 && value < 127) {
            return std::string("'") + static_cast<char>(value) + "'";
        }
        return std::to_string(static_cast<int>(value));
    }

    const std::uint64_t zero_extended = load_bits(bytes, little_endian);
    char buffer[32] = {};
    if (is_unsigned_name(name)) {
        std::snprintf(buffer, sizeof(buffer), "%llu",
                      static_cast<unsigned long long>(zero_extended));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld",
                      static_cast<long long>(sign_extend(zero_extended, bytes.size())));
    }
    return buffer;
}

std::vector<MemberView> expand_members(const DebugInfo& info, TypeId type_id,
                                       std::span<const std::uint8_t> storage,
                                       bool little_endian) {
    std::vector<MemberView> result;
    const TypeInfo* type = find_type(info, type_id);
    if (type == nullptr) {
        return result;
    }
    result.reserve(type->members.size());
    for (const MemberInfo& member : type->members) {
        MemberView view;
        view.name = member.name;
        view.type = member.type;
        const TypeInfo* member_type = find_type(info, member.type);
        view.type_name = member_type != nullptr ? member_type->name : "?";
        const std::size_t size = member_type != nullptr ? member_type->size : 0;
        if (size == 0 || member.offset > storage.size() ||
            size > storage.size() - member.offset) {
            view.value = "?";
            result.push_back(std::move(view));
            continue;
        }
        const auto member_bytes = storage.subspan(member.offset, size);
        view.storage.assign(member_bytes.begin(), member_bytes.end());
        if (member_type != nullptr && member_type->kind == TypeKind::Array) {
            view.value = "[" + std::to_string(member_type->element_count) + "]";
            view.has_children = true;
        } else if (member_type != nullptr &&
                   (member_type->kind == TypeKind::Struct ||
                    member_type->kind == TypeKind::Union)) {
            view.value = "{...}";
            view.has_children = true;
        } else {
            view.value = format_scalar(member_type, member_bytes, little_endian);
        }
        result.push_back(std::move(view));
    }
    return result;
}

} // namespace krdbg::detail
