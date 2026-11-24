#include "test_framework.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "json_lite.h"
#include "krdbg/kdbg.h"

namespace {

std::uint32_t crc32(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

// Recomputes the header CRC after a test mutates a header field. The CRC covers
// the whole payload with the CRC field itself zeroed.
void fix_header_crc(std::vector<std::uint8_t>& payload) {
    payload[52] = 0;
    payload[53] = 0;
    payload[54] = 0;
    payload[55] = 0;
    const std::uint32_t checksum = crc32(payload);
    payload[52] = static_cast<std::uint8_t>(checksum & 0xFFU);
    payload[53] = static_cast<std::uint8_t>((checksum >> 8) & 0xFFU);
    payload[54] = static_cast<std::uint8_t>((checksum >> 16) & 0xFFU);
    payload[55] = static_cast<std::uint8_t>((checksum >> 24) & 0xFFU);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void write_u32(std::vector<std::uint8_t>& data, std::size_t offset,
               std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
}

std::vector<std::uint8_t> read_binary(const std::string& name) {
    const std::filesystem::path path =
        std::filesystem::path(KRDBG_FIXTURE_DIR) / name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
}

std::string read_text(const std::string& name) {
    const std::filesystem::path path =
        std::filesystem::path(KRDBG_FIXTURE_DIR) / name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

std::string kind_name(krdbg::TypeKind kind) {
    switch (kind) {
    case krdbg::TypeKind::Builtin: return "builtin";
    case krdbg::TypeKind::Pointer: return "pointer";
    case krdbg::TypeKind::Array: return "array";
    case krdbg::TypeKind::Struct: return "struct";
    case krdbg::TypeKind::Union: return "union";
    case krdbg::TypeKind::Enum: return "enum";
    case krdbg::TypeKind::Function: return "function";
    case krdbg::TypeKind::Unknown: return "unknown";
    }
    return "unknown";
}

std::string type_name(const krdbg::DebugInfo& info, krdbg::TypeId id) {
    return id < info.types.size() ? info.types[id].name : std::string{};
}

const krdbg::VariableInfo* find_variable(const krdbg::DebugInfo& info,
                                         const std::string& name) {
    for (const krdbg::VariableInfo& variable : info.variables) {
        if (variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

} // namespace

KRDBG_TEST(kdbg_section_translates_the_k32_fixture) {
    const std::vector<std::uint8_t> container = read_binary("fake.kr32");
    CHECK(!container.empty());
    krdbg::DebugInfo info;
    std::string error;
    CHECK(krdbg::kdbg::read_section(container, info, error));
    CHECK(error.empty());

    CHECK_EQ(info.target_id, 1U);
    CHECK_EQ(info.abi_version, 1U);
    CHECK_EQ(info.compiler, "krc");
    CHECK_EQ(info.pointer_bits, 32U);

    CHECK_EQ(info.sources.size(), 1U);
    CHECK_EQ(info.sources[0].id, 0U);
    CHECK_EQ(info.sources[0].path, "tests/fixtures/kdbg/fake.c");
    CHECK_EQ(info.sources[0].name, "fake.c");

    CHECK_EQ(info.functions.size(), 2U);
    CHECK_EQ(info.functions[0].name, "util_add");
    CHECK_EQ(info.functions[0].begin, 0x10000U);
    CHECK_EQ(info.functions[1].name, "_start");
    CHECK_EQ(info.functions[1].begin, 0x1006CU);
    CHECK(info.lines.size() > 0U);

    CHECK_EQ(info.types.size(), 2U);
    CHECK_EQ(info.types[0].name, "i32");
    CHECK_EQ(info.types[1].name, "Point");
    CHECK_EQ(info.types[1].kind, krdbg::TypeKind::Struct);
    CHECK_EQ(info.types[1].size, 8U);
    CHECK_EQ(info.types[1].members.size(), 2U);

    const krdbg::VariableInfo* total = find_variable(info, "total");
    CHECK(total != nullptr);
    CHECK(total != nullptr && total->location.kind == krdbg::LocationKind::FrameOffset);
    CHECK(total != nullptr && total->location.frame_offset == 8);
    CHECK(total != nullptr && total->size == 4U);
    CHECK(total != nullptr && type_name(info, total->type) == "i32");
    const krdbg::VariableInfo* point = find_variable(info, "p");
    CHECK(point != nullptr);
    CHECK(point != nullptr && point->location.frame_offset == 20);
    CHECK(point != nullptr && type_name(info, point->type) == "Point");
}

KRDBG_TEST(kdbg_payload_and_section_translate_identically) {
    const std::vector<std::uint8_t> payload = read_binary("fake.kdbg");
    const std::vector<std::uint8_t> container = read_binary("fake.kr32");
    CHECK(!payload.empty());
    CHECK(!container.empty());

    krdbg::DebugInfo from_payload;
    krdbg::DebugInfo from_section;
    std::string error;
    CHECK(krdbg::kdbg::read_payload(payload, from_payload, error));
    CHECK(krdbg::kdbg::read_section(container, from_section, error));

    CHECK_EQ(from_payload.target_id, from_section.target_id);
    CHECK_EQ(from_payload.functions.size(), from_section.functions.size());
    CHECK_EQ(from_payload.variables.size(), from_section.variables.size());
    CHECK_EQ(from_payload.types.size(), from_section.types.size());
    CHECK_EQ(from_payload.sources.size(), from_section.sources.size());
    CHECK_EQ(from_payload.sources[0].path, from_section.sources[0].path);
}

KRDBG_TEST(kdbg_translation_matches_krc_json_for_every_carried_field) {
    const std::vector<std::uint8_t> container = read_binary("fake.kr32");
    krdbg::DebugInfo info;
    std::string error;
    CHECK(krdbg::kdbg::read_section(container, info, error));

    test_json::Value expected;
    const std::string json = read_text("fake.krc.json");
    CHECK(!json.empty());
    std::string json_error;
    const bool parsed = test_json::parse(json, expected, json_error);
    CHECK(parsed);
    if (!parsed) {
        return;
    }

    CHECK_EQ(expected["target"].number, static_cast<long long>(info.target_id));
    CHECK_EQ(expected["abi"].number, static_cast<long long>(info.abi_version));
    CHECK_EQ(expected["minor"].number, 1);
    CHECK_EQ(expected["pointer"].number,
             static_cast<long long>(info.pointer_bits / 8U));
    CHECK_EQ(expected["compiler"].text, info.compiler);

    const auto& sources = expected["sources"].array;
    CHECK_EQ(sources.size(), info.sources.size());
    for (std::size_t index = 0; index < sources.size() && index < info.sources.size();
         ++index) {
        CHECK_EQ(sources[index]["id"].number,
                 static_cast<long long>(info.sources[index].id));
        CHECK_EQ(sources[index]["path"].text, info.sources[index].path);
    }

    const auto& functions = expected["functions"].array;
    CHECK_EQ(functions.size(), info.functions.size());
    for (std::size_t index = 0; index < functions.size() &&
                                index < info.functions.size();
         ++index) {
        CHECK_EQ(functions[index]["name"].text, info.functions[index].name);
        CHECK_EQ(functions[index]["start"].number,
                 static_cast<long long>(info.functions[index].begin));
        CHECK_EQ(functions[index]["end"].number,
                 static_cast<long long>(info.functions[index].end));
    }

    const auto& variables = expected["variables"].array;
    // Locals come first; data symbols are appended as absolute globals and are
    // not part of krc's `variables` dump.
    CHECK(info.variables.size() >= variables.size());
    for (std::size_t index = 0; index < variables.size() &&
                                index < info.variables.size();
         ++index) {
        const krdbg::VariableInfo& variable = info.variables[index];
        const test_json::Value& want = variables[index];
        CHECK_EQ(want["function"].text, variable.function);
        CHECK_EQ(want["name"].text, variable.name);
        CHECK_EQ(want["offset"].number, variable.location.frame_offset);
        CHECK_EQ(want["size"].number, static_cast<long long>(variable.size));
        CHECK_EQ(want["type"].text, type_name(info, variable.type));
    }

    // KDBG v1.1 carries parameter/local/static metadata and the owning scope.
    const auto& variable_info = expected["variableInfo"].array;
    for (const test_json::Value& want : variable_info) {
        const auto found = std::find_if(
            info.variables.begin(), info.variables.end(),
            [&want](const krdbg::VariableInfo& variable) {
                return variable.function == want["function"].text &&
                       variable.name == want["name"].text;
            });
        CHECK(found != info.variables.end());
        if (found == info.variables.end()) {
            continue;
        }
        CHECK_EQ(want["scope"].text, found->scope);
        if (want["kind"].text == "param") {
            CHECK((found->flags & krdbg::VariableParam) != 0U);
        } else if (want["kind"].text == "local") {
            CHECK((found->flags & krdbg::VariableLocal) != 0U);
        } else if (want["kind"].text == "static") {
            CHECK((found->flags & krdbg::VariableStatic) != 0U);
        }
    }

    const auto& types = expected["types"].array;
    CHECK_EQ(types.size(), info.types.size());
    for (std::size_t index = 0; index < types.size() && index < info.types.size();
         ++index) {
        const krdbg::TypeInfo& type = info.types[index];
        const test_json::Value& want = types[index];
        CHECK_EQ(want["name"].text, type.name);
        CHECK_EQ(want["size"].number, static_cast<long long>(type.size));
        CHECK_EQ(want["kind"].text, kind_name(type.kind));
        CHECK_EQ(want["members"].array.size(), type.members.size());
        for (std::size_t member = 0;
             member < want["members"].array.size() && member < type.members.size();
             ++member) {
            CHECK_EQ(want["members"].array[member]["name"].text,
                     type.members[member].name);
            CHECK_EQ(want["members"].array[member]["offset"].number,
                     static_cast<long long>(type.members[member].offset));
            CHECK_EQ(want["members"].array[member]["type"].text,
                     type_name(info, type.members[member].type));
        }
    }
}

KRDBG_TEST(kdbg_rejects_truncated_bad_crc_and_bad_version) {
    const std::vector<std::uint8_t> payload = read_binary("fake.kdbg");
    CHECK(payload.size() > 128U);
    krdbg::DebugInfo info;
    std::string error;

    const std::span<const std::uint8_t> shorter(payload.data(),
                                                std::min<std::size_t>(32, payload.size()));
    CHECK(!krdbg::kdbg::read_payload(shorter, info, error));
    CHECK(error.find("shorter") != std::string::npos);

    std::vector<std::uint8_t> bad_magic = payload;
    bad_magic[0] = 'X';
    CHECK(!krdbg::kdbg::read_payload(bad_magic, info, error));
    CHECK(error.find("magic") != std::string::npos);

    std::vector<std::uint8_t> bad_version = payload;
    bad_version[4] = 2;
    bad_version[5] = 0;
    CHECK(!krdbg::kdbg::read_payload(bad_version, info, error));
    CHECK(error.find("version") != std::string::npos);

    std::vector<std::uint8_t> bad_crc = payload;
    bad_crc[100] ^= 0xFFU;
    CHECK(!krdbg::kdbg::read_payload(bad_crc, info, error));
    CHECK(error.find("checksum") != std::string::npos);
}

KRDBG_TEST(kdbg_rejects_a_bad_section_table_and_a_missing_section) {
    const std::vector<std::uint8_t> payload = read_binary("fake.kdbg");
    krdbg::DebugInfo info;
    std::string error;

    std::vector<std::uint8_t> bad_table = payload;
    bad_table[40] = 0xFF;
    bad_table[41] = 0xFF;
    bad_table[42] = 0xFF;
    bad_table[43] = 0xFF;
    fix_header_crc(bad_table);
    CHECK(!krdbg::kdbg::read_payload(bad_table, info, error));
    CHECK(error.find("section table") != std::string::npos);

    // A record length beyond its section is the "bad record length" case. Both
    // the section CRC and the header CRC must be valid for the record parser to
    // be reached.
    std::vector<std::uint8_t> bad_record = payload;
    const std::uint32_t table = read_u32(bad_record, 40);
    const std::uint32_t section_offset = read_u32(bad_record, table + 8);
    const std::uint32_t section_size = read_u32(bad_record, table + 12);
    CHECK(section_size > 8U);
    write_u32(bad_record, section_offset + 4, 0xFFFFFFFFU);
    write_u32(bad_record, table + 16,
              crc32(std::span<const std::uint8_t>(bad_record).subspan(section_offset,
                                                                      section_size)));
    fix_header_crc(bad_record);
    CHECK(!krdbg::kdbg::read_payload(bad_record, info, error));
    CHECK(error.find("record") != std::string::npos);

    const std::vector<std::uint8_t> zeros(64, 0);
    CHECK(!krdbg::kdbg::read_section(zeros, info, error));
    CHECK(!error.empty());

    // Rename the container section so find_kdbg_section cannot find it.
    std::vector<std::uint8_t> renamed = read_binary("fake.kr32");
    const std::array<std::uint8_t, 6> name{'.', 'k', 'd', 'b', 'g', '\0'};
    const auto found = std::search(renamed.begin(), renamed.end(), name.begin(), name.end());
    CHECK(found != renamed.end());
    if (found != renamed.end()) {
        *found = '.';
        *(found + 1) = 'x';
        CHECK(!krdbg::kdbg::read_section(renamed, info, error));
        CHECK(error.find("not found") != std::string::npos);
    }
}
