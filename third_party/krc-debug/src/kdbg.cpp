#include "krc/debug/kdbg.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace krc::debug {
namespace {

constexpr std::uint32_t kHeaderSize = 64;
constexpr std::uint32_t kSectionEntrySize = 32;
constexpr std::uint32_t kRecordHeaderSize = 8;
constexpr std::uint32_t kCrcOffset = 52;

enum class Section : std::uint16_t {
    Source = 1,
    Line = 2,
    Function = 3,
    Symbol = 4,
    Variable = 5,
    Type = 6,
    Build = 7,
};

enum class Record : std::uint16_t {
    Source = 1,
    Line = 2,
    Function = 3,
    Symbol = 4,
    Variable = 5,
    Type = 6,
    Member = 7,
    Build = 8,
};

std::uint32_t crc32(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void put_u16(std::vector<std::uint8_t>& data, std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>(value));
    data.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put_u32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        data.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
    }
}

void put_u64(std::vector<std::uint8_t>& data, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        data.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
    }
}

void set_u16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void set_u32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        data[offset + static_cast<std::size_t>(byte)] =
            static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

void set_u64(std::vector<std::uint8_t>& data, std::size_t offset, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        data[offset + static_cast<std::size_t>(byte)] =
            static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    std::uint32_t value = 0;
    for (int byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(data[offset + static_cast<std::size_t>(byte)])
                 << (byte * 8);
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t offset) {
    std::uint64_t value = 0;
    for (int byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(data[offset + static_cast<std::size_t>(byte)])
                 << (byte * 8);
    }
    return value;
}

class StringPool {
public:
    std::uint32_t add(const std::string& value) {
        const auto found = offsets_.find(value);
        if (found != offsets_.end()) {
            return found->second;
        }
        const auto offset = static_cast<std::uint32_t>(data_.size());
        offsets_.emplace(value, offset);
        data_.insert(data_.end(), value.begin(), value.end());
        data_.push_back(0);
        return offset;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return data_; }

private:
    std::unordered_map<std::string, std::uint32_t> offsets_;
    std::vector<std::uint8_t> data_;
};

std::string string_at(std::span<const std::uint8_t> table, std::uint32_t offset) {
    if (offset >= table.size()) {
        return {};
    }
    std::size_t end = offset;
    while (end < table.size() && table[end] != 0) {
        ++end;
    }
    return std::string(reinterpret_cast<const char*>(table.data() + offset), end - offset);
}

struct SectionData {
    std::string name;
    Section type;
    std::vector<std::uint8_t> data;
};

void record(std::vector<std::uint8_t>& out, Record tag,
            const std::vector<std::uint8_t>& payload) {
    put_u16(out, static_cast<std::uint16_t>(tag));
    put_u16(out, 0);
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::uint16_t type_kind(const std::string& kind) {
    if (kind == "struct") return static_cast<std::uint16_t>(1);
    if (kind == "union") return static_cast<std::uint16_t>(2);
    if (kind == "pointer") return static_cast<std::uint16_t>(3);
    if (kind == "array") return static_cast<std::uint16_t>(4);
    return static_cast<std::uint16_t>(0);
}

std::string kind_name(std::uint16_t kind) {
    switch (kind) {
    case 1:
        return "struct";
    case 2:
        return "union";
    case 3:
        return "pointer";
    case 4:
        return "array";
    default:
        return "builtin";
    }
}

std::string json_escape(const std::string& text) {
    std::string out;
    for (const char character : text) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += character;
            break;
        }
    }
    return out;
}

}  // namespace

std::vector<std::uint8_t> encode_kdbg(const KdbgImage& image) {
    StringPool strings;
    const std::array<std::string, 7> section_names = {
        ".kdbg_src", ".kdbg_line", ".kdbg_func", ".kdbg_sym",
        ".kdbg_var", ".kdbg_type", ".kdbg_build"};
    for (const auto& name : section_names) {
        strings.add(name);
    }
    for (const auto& source : image.sources) strings.add(source.path);
    for (const auto& function : image.functions) {
        strings.add(function.name);
        strings.add(function.type);
    }
    for (const auto& symbol : image.symbols) {
        strings.add(symbol.name);
        strings.add(symbol.type);
    }
    for (const auto& variable : image.variables) {
        strings.add(variable.function);
        strings.add(variable.name);
        strings.add(variable.type);
    }
    for (const auto& type : image.types) {
        strings.add(type.name);
        for (const auto& member : type.members) {
            strings.add(member.name);
            strings.add(member.type);
        }
    }
    strings.add(image.compiler);

    std::array<std::vector<std::uint8_t>, 7> payloads;
    for (const auto& source : image.sources) {
        std::vector<std::uint8_t> data;
        put_u32(data, source.id);
        put_u32(data, strings.add(source.path));
        record(payloads[0], Record::Source, data);
    }
    for (const auto& line : image.lines) {
        std::vector<std::uint8_t> data;
        put_u32(data, line.address);
        put_u32(data, line.source);
        put_u32(data, line.line);
        put_u32(data, line.column);
        record(payloads[1], Record::Line, data);
    }
    for (const auto& function : image.functions) {
        std::vector<std::uint8_t> data;
        put_u32(data, strings.add(function.name));
        put_u32(data, function.start);
        put_u32(data, function.end);
        put_u32(data, function.frame);
        put_u32(data, strings.add(function.type));
        record(payloads[2], Record::Function, data);
    }
    for (const auto& symbol : image.symbols) {
        std::vector<std::uint8_t> data;
        put_u32(data, strings.add(symbol.name));
        put_u32(data, symbol.address);
        put_u32(data, symbol.size);
        put_u32(data, strings.add(symbol.type));
        record(payloads[3], Record::Symbol, data);
    }
    for (const auto& variable : image.variables) {
        std::vector<std::uint8_t> data;
        put_u32(data, strings.add(variable.function));
        put_u32(data, strings.add(variable.name));
        put_u32(data, variable.offset);
        put_u32(data, strings.add(variable.type));
        put_u32(data, variable.size);
        record(payloads[4], Record::Variable, data);
    }
    for (const auto& type : image.types) {
        std::vector<std::uint8_t> data;
        put_u32(data, strings.add(type.name));
        put_u32(data, type.size);
        put_u16(data, type_kind(type.kind));
        put_u16(data, 0);
        put_u32(data, static_cast<std::uint32_t>(type.members.size()));
        record(payloads[5], Record::Type, data);
        for (const auto& member : type.members) {
            std::vector<std::uint8_t> member_data;
            put_u32(member_data, strings.add(member.name));
            put_u32(member_data, member.offset);
            put_u32(member_data, strings.add(member.type));
            put_u32(member_data, member.size);
            record(payloads[5], Record::Member, member_data);
        }
    }
    {
        std::vector<std::uint8_t> data;
        put_u32(data, strings.add(image.compiler));
        put_u64(data, image.build_id);
        put_u32(data, static_cast<std::uint32_t>(image.sources.size()));
        put_u32(data, 0);
        record(payloads[6], Record::Build, data);
    }

    std::vector<SectionData> sections;
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        if (payloads[index].empty()) {
            continue;
        }
        sections.push_back({section_names[index], static_cast<Section>(index + 1),
                            std::move(payloads[index])});
    }

    std::vector<std::uint8_t> output(kHeaderSize, 0);
    std::vector<std::uint8_t> table;
    for (const auto& section : sections) {
        const auto offset = static_cast<std::uint32_t>(output.size());
        output.insert(output.end(), section.data.begin(), section.data.end());
        put_u32(table, strings.add(section.name));
        put_u16(table, static_cast<std::uint16_t>(section.type));
        put_u16(table, 0);
        put_u32(table, offset);
        put_u32(table, static_cast<std::uint32_t>(section.data.size()));
        put_u32(table, crc32(section.data));
        put_u32(table, 0);
        put_u32(table, 0);
        put_u32(table, 0);
    }
    const auto section_table_offset = static_cast<std::uint32_t>(output.size());
    output.insert(output.end(), table.begin(), table.end());
    const auto string_table_offset = static_cast<std::uint32_t>(output.size());
    output.insert(output.end(), strings.data().begin(), strings.data().end());

    const std::array<char, 4> magic = {'K', 'D', 'B', 'G'};
    for (std::size_t index = 0; index < magic.size(); ++index) {
        output[index] = static_cast<std::uint8_t>(magic[index]);
    }
    set_u16(output, 4, 1);
    set_u16(output, 6, 0);
    set_u32(output, 8, kHeaderSize);
    set_u32(output, 12, 0);
    set_u32(output, 16, image.target_id);
    set_u32(output, 20, image.abi_version);
    output[24] = image.pointer_width;
    output[25] = image.endianness;
    set_u16(output, 26, 0);
    for (int byte = 0; byte < 8; ++byte) {
        output[28 + static_cast<std::size_t>(byte)] =
            static_cast<std::uint8_t>(image.build_id >> (byte * 8));
    }
    set_u16(output, 36, static_cast<std::uint16_t>(sections.size()));
    set_u16(output, 38, 0);
    set_u32(output, 40, section_table_offset);
    set_u32(output, 44, string_table_offset);
    set_u32(output, 48, static_cast<std::uint32_t>(strings.data().size()));
    set_u32(output, 52, 0);
    // The eight reserved header bytes carry the image base the payload's
    // absolute addresses were built for. ADR 0009 records the use; readers that
    // skip reserved bytes are unaffected.
    set_u64(output, 56, image.image_base);
    const auto checksum = crc32(output);
    set_u32(output, kCrcOffset, checksum);
    return output;
}

bool decode_kdbg(std::span<const std::uint8_t> data, KdbgImage& image, std::string& error) {
    image = KdbgImage{};
    if (data.size() < kHeaderSize) {
        error = "KDBG is shorter than its header";
        return false;
    }
    if (std::memcmp(data.data(), "KDBG", 4) != 0) {
        error = "KDBG magic is missing";
        return false;
    }
    const auto major = read_u16(data, 4);
    if (major != 1) {
        error = "unsupported KDBG major version";
        return false;
    }
    const auto header_size = read_u32(data, 8);
    if (header_size < kHeaderSize || header_size > data.size()) {
        error = "invalid KDBG header size";
        return false;
    }
    std::vector<std::uint8_t> checksum_data(data.begin(), data.end());
    const auto expected = read_u32(checksum_data, kCrcOffset);
    set_u32(checksum_data, kCrcOffset, 0);
    if (crc32(checksum_data) != expected) {
        error = "KDBG header checksum mismatch";
        return false;
    }
    image.target_id = read_u32(data, 16);
    image.abi_version = read_u32(data, 20);
    image.pointer_width = data[24];
    image.endianness = data[25];
    image.build_id = read_u64(data, 28);
    image.image_base = read_u64(data, 56);
    const auto section_count = read_u16(data, 36);
    const auto table_offset = read_u32(data, 40);
    const auto string_offset = read_u32(data, 44);
    const auto string_size = read_u32(data, 48);
    if (table_offset > data.size() || section_count > (data.size() - table_offset) / kSectionEntrySize) {
        error = "KDBG section table is out of bounds";
        return false;
    }
    if (string_offset > data.size() || string_size > data.size() - string_offset) {
        error = "KDBG string table is out of bounds";
        return false;
    }
    const auto strings = data.subspan(string_offset, string_size);

    for (std::size_t index = 0; index < section_count; ++index) {
        const auto entry = table_offset + index * kSectionEntrySize;
        const auto name = string_at(strings, read_u32(data, entry));
        const auto type = static_cast<Section>(read_u16(data, entry + 4));
        const auto offset = read_u32(data, entry + 8);
        const auto size = read_u32(data, entry + 12);
        const auto checksum = read_u32(data, entry + 16);
        if (offset > data.size() || size > data.size() - offset) {
            error = "KDBG section '" + name + "' is out of bounds";
            return false;
        }
        const auto section = data.subspan(offset, size);
        if (crc32(section) != checksum) {
            error = "KDBG section '" + name + "' checksum mismatch";
            return false;
        }
        std::size_t position = 0;
        std::string current_type;
        while (position + kRecordHeaderSize <= section.size()) {
            const auto tag = read_u16(section, position);
            const auto length = read_u32(section, position + 4);
            position += kRecordHeaderSize;
            if (length > section.size() - position) {
                error = "KDBG record exceeds its section";
                return false;
            }
            const auto payload = section.subspan(position, length);
            position += length;
            switch (static_cast<Record>(tag)) {
            case Record::Source:
                if (payload.size() >= 8) {
                    image.sources.push_back(
                        {read_u32(payload, 0), string_at(strings, read_u32(payload, 4))});
                }
                break;
            case Record::Line:
                if (payload.size() >= 16) {
                    image.lines.push_back({read_u32(payload, 0), read_u32(payload, 4),
                                           read_u32(payload, 8), read_u32(payload, 12)});
                }
                break;
            case Record::Function:
                if (payload.size() >= 20) {
                    image.functions.push_back(
                        {string_at(strings, read_u32(payload, 0)), read_u32(payload, 4),
                         read_u32(payload, 8), read_u32(payload, 12),
                         string_at(strings, read_u32(payload, 16))});
                }
                break;
            case Record::Symbol:
                if (payload.size() >= 16) {
                    image.symbols.push_back(
                        {string_at(strings, read_u32(payload, 0)), read_u32(payload, 4),
                         read_u32(payload, 8), string_at(strings, read_u32(payload, 12))});
                }
                break;
            case Record::Variable:
                if (payload.size() >= 20) {
                    image.variables.push_back(
                        {string_at(strings, read_u32(payload, 0)),
                         string_at(strings, read_u32(payload, 4)), read_u32(payload, 8),
                         read_u32(payload, 16),
                         string_at(strings, read_u32(payload, 12))});
                }
                break;
            case Record::Type:
                if (payload.size() >= 16) {
                    current_type = string_at(strings, read_u32(payload, 0));
                    KdbgType kdbg_type;
                    kdbg_type.name = current_type;
                    kdbg_type.size = read_u32(payload, 4);
                    kdbg_type.kind = kind_name(read_u16(payload, 8));
                    image.types.push_back(std::move(kdbg_type));
                }
                break;
            case Record::Member:
                if (payload.size() >= 16 && !image.types.empty()) {
                    image.types.back().members.push_back(
                        {string_at(strings, read_u32(payload, 0)), read_u32(payload, 4),
                         read_u32(payload, 12), string_at(strings, read_u32(payload, 8))});
                }
                break;
            case Record::Build:
                if (payload.size() >= 16) {
                    image.compiler = string_at(strings, read_u32(payload, 0));
                    image.build_id = read_u64(payload, 4);
                }
                break;
            default:
                break;
            }
            static_cast<void>(type);
        }
    }
    return true;
}

std::string dump_kdbg_text(const KdbgImage& image) {
    std::ostringstream out;
    out << "KDBG target=" << image.target_id << " abi=" << image.abi_version
        << " sources=" << image.sources.size() << " lines=" << image.lines.size()
        << " functions=" << image.functions.size() << " symbols=" << image.symbols.size()
        << " variables=" << image.variables.size() << " types=" << image.types.size()
        << '\n';
    for (const auto& source : image.sources) {
        out << "source " << source.id << " " << source.path << '\n';
    }
    // Line records are the largest part of an image and were the only kind the dump
    // never printed, so nothing could assert that a source line maps to an address.
    for (const auto& line : image.lines) {
        out << "line 0x" << std::hex << line.address << std::dec << " source="
            << line.source << " line=" << line.line << " column=" << line.column << '\n';
    }
    for (const auto& function : image.functions) {
        out << "function " << function.name << " 0x" << std::hex << function.start << "-0x"
            << function.end << std::dec << " frame=" << function.frame
            << " type=" << function.type << '\n';
    }
    for (const auto& symbol : image.symbols) {
        out << "symbol " << symbol.name << " 0x" << std::hex << symbol.address << std::dec
            << " size=" << symbol.size << " type=" << symbol.type << '\n';
    }
    for (const auto& type : image.types) {
        out << "type " << type.name << " size=" << type.size << " kind=" << type.kind
            << '\n';
        for (const auto& member : type.members) {
            out << "  member " << member.name << " offset=" << member.offset
                << " type=" << member.type << '\n';
        }
    }
    for (const auto& variable : image.variables) {
        out << "variable " << variable.function << "." << variable.name
            << " offset=" << variable.offset << " type=" << variable.type
            << " size=" << variable.size << '\n';
    }
    return out.str();
}

std::string dump_kdbg_json(const KdbgImage& image) {
    std::ostringstream out;
    out << "{\"target\":" << image.target_id << ",\"abi\":" << image.abi_version
        << ",\"compiler\":\"" << json_escape(image.compiler) << "\",";
    out << "\"sources\":[";
    for (std::size_t index = 0; index < image.sources.size(); ++index) {
        if (index != 0) out << ',';
        out << "{\"id\":" << image.sources[index].id << ",\"path\":\""
            << json_escape(image.sources[index].path) << "\"}";
    }
    out << "],\"functions\":[";
    for (std::size_t index = 0; index < image.functions.size(); ++index) {
        if (index != 0) out << ',';
        const auto& function = image.functions[index];
        out << "{\"name\":\"" << json_escape(function.name) << "\",\"start\":"
            << function.start << ",\"end\":" << function.end
            << ",\"frame\":" << function.frame << ",\"type\":\""
            << json_escape(function.type) << "\"}";
    }
    out << "],\"variables\":[";
    for (std::size_t index = 0; index < image.variables.size(); ++index) {
        if (index != 0) out << ',';
        const auto& variable = image.variables[index];
        out << "{\"function\":\"" << json_escape(variable.function) << "\",\"name\":\""
            << json_escape(variable.name) << "\",\"offset\":" << variable.offset
            << ",\"size\":" << variable.size << ",\"type\":\""
            << json_escape(variable.type) << "\"}";
    }
    out << "],\"types\":[";
    for (std::size_t index = 0; index < image.types.size(); ++index) {
        if (index != 0) out << ',';
        const auto& type = image.types[index];
        out << "{\"name\":\"" << json_escape(type.name) << "\",\"size\":" << type.size
            << ",\"kind\":\"" << json_escape(type.kind) << "\",\"members\":[";
        for (std::size_t member_index = 0; member_index < type.members.size();
             ++member_index) {
            if (member_index != 0) out << ',';
            const auto& member = type.members[member_index];
            out << "{\"name\":\"" << json_escape(member.name) << "\",\"offset\":"
                << member.offset << ",\"type\":\"" << json_escape(member.type)
                << "\",\"size\":" << member.size << "}";
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

bool find_kdbg_section(std::span<const std::uint8_t> image, KdbgSection& section,
                       std::string& error) {
    section = KdbgSection{};
    if (image.size() < 4) {
        error = "image is shorter than its magic";
        return false;
    }
    const bool kr32 = image[0] == 'K' && image[1] == '3' && image[2] == '2';
    const bool kex = image[0] == 'K' && image[1] == 'R' && image[2] == '1' &&
                     image[3] == '6';
    if (!kr32 && !kex) {
        error = "unrecognised image container";
        return false;
    }
    if (image.size() < kHeaderSize) {
        error = "image is shorter than its header";
        return false;
    }

    std::uint32_t section_count = 0;
    std::uint32_t table_offset = 0;
    std::uint32_t string_offset = 0;
    if (kr32) {
        section.container = "k32";
        section.image_base = read_u32(image, 28);
        section_count = read_u32(image, 12);
        table_offset = kHeaderSize;
        string_offset = read_u32(image, 32);
    } else {
        section.container = "kex";
        section_count = read_u16(image, 20);
        table_offset = read_u32(image, 28);
        string_offset = read_u32(image, 36);
        if (section_count > 0) {
            const auto first = table_offset;
            if (first + kSectionEntrySize > image.size()) {
                error = "KR16 section table is out of bounds";
                return false;
            }
            section.image_base = read_u16(image, first + 8);
        }
    }
    if (string_offset > image.size()) {
        error = "image string table is out of bounds";
        return false;
    }
    if (table_offset > image.size() ||
        section_count > (image.size() - table_offset) / kSectionEntrySize) {
        error = "image section table is out of bounds";
        return false;
    }
    const auto strings = image.subspan(string_offset);
    for (std::size_t index = 0; index < section_count; ++index) {
        const auto entry = table_offset + index * kSectionEntrySize;
        const auto name = string_at(strings, read_u32(image, entry));
        if (name != ".kdbg") {
            continue;
        }
        const auto payload = read_u32(image, entry + 12);
        const auto size = read_u32(image, entry + 16);
        if (payload > image.size() || size > image.size() - payload) {
            error = "section '.kdbg' is out of bounds";
            return false;
        }
        section.payload = image.subspan(payload, size);
        return true;
    }
    error = "section '.kdbg' was not found";
    return false;
}

bool decode_kdbg_image(std::span<const std::uint8_t> container, KdbgImage& image,
                       std::string& error) {
    KdbgSection section;
    if (!find_kdbg_section(container, section, error)) {
        return false;
    }
    KdbgImage decoded;
    if (!decode_kdbg(section.payload, decoded, error)) {
        return false;
    }
    if (decoded.image_base != 0 && decoded.image_base != section.image_base) {
        std::ostringstream message;
        message << "KDBG image base 0x" << std::hex << decoded.image_base
                << " does not match the image's load base 0x" << section.image_base;
        error = message.str();
        return false;
    }
    image = std::move(decoded);
    return true;
}

}  // namespace krc::debug
