#include "settings_json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace krdbg::settings_json {
namespace {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};

class Parser {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    bool parse(Json& out, std::string& error) {
        skip_space();
        if (!parse_value(out)) {
            error = error_ + " at byte " + std::to_string(position_);
            return false;
        }
        skip_space();
        if (position_ != input_.size()) {
            error = "trailing data at byte " + std::to_string(position_);
            return false;
        }
        return true;
    }

private:
    void skip_space() {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                ++position_;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) {
        skip_space();
        if (position_ >= input_.size() || input_[position_] != expected) {
            return fail(std::string("expected '") + expected + "'");
        }
        ++position_;
        return true;
    }

    bool fail(const std::string& message) {
        if (error_.empty()) {
            error_ = message;
        }
        return false;
    }

    bool parse_value(Json& out) {
        skip_space();
        if (position_ >= input_.size()) {
            return fail("unexpected end of input");
        }
        switch (input_[position_]) {
        case '{': return parse_object(out);
        case '[': return parse_array(out);
        case '"':
            out.type = Json::Type::String;
            return parse_string(out.text);
        case 't':
            if (input_.substr(position_, 4) != "true") {
                return fail("invalid literal");
            }
            position_ += 4;
            out.type = Json::Type::Bool;
            out.boolean = true;
            return true;
        case 'f':
            if (input_.substr(position_, 5) != "false") {
                return fail("invalid literal");
            }
            position_ += 5;
            out.type = Json::Type::Bool;
            out.boolean = false;
            return true;
        case 'n':
            if (input_.substr(position_, 4) != "null") {
                return fail("invalid literal");
            }
            position_ += 4;
            out.type = Json::Type::Null;
            return true;
        default:
            return parse_number(out);
        }
    }

    bool parse_object(Json& out) {
        if (!consume('{')) {
            return false;
        }
        out.type = Json::Type::Object;
        skip_space();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return true;
        }
        while (true) {
            skip_space();
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            if (!consume(':')) {
                return false;
            }
            Json value;
            if (!parse_value(value)) {
                return false;
            }
            out.object[std::move(key)] = std::move(value);
            skip_space();
            if (position_ < input_.size() && input_[position_] == ',') {
                ++position_;
                continue;
            }
            return consume('}');
        }
    }

    bool parse_array(Json& out) {
        if (!consume('[')) {
            return false;
        }
        out.type = Json::Type::Array;
        skip_space();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return true;
        }
        while (true) {
            Json value;
            if (!parse_value(value)) {
                return false;
            }
            out.array.push_back(std::move(value));
            skip_space();
            if (position_ < input_.size() && input_[position_] == ',') {
                ++position_;
                continue;
            }
            return consume(']');
        }
    }

    bool parse_string(std::string& out) {
        skip_space();
        if (position_ >= input_.size() || input_[position_] != '"') {
            return fail("expected string");
        }
        ++position_;
        out.clear();
        while (position_ < input_.size()) {
            const char ch = input_[position_++];
            if (ch == '"') {
                return true;
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (position_ >= input_.size()) {
                return fail("unterminated escape");
            }
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > input_.size()) {
                    return fail("short unicode escape");
                }
                unsigned value = 0;
                for (int index = 0; index < 4; ++index) {
                    const char digit = input_[position_++];
                    value <<= 4;
                    if (digit >= '0' && digit <= '9') {
                        value |= static_cast<unsigned>(digit - '0');
                    } else if (digit >= 'a' && digit <= 'f') {
                        value |= static_cast<unsigned>(digit - 'a' + 10);
                    } else if (digit >= 'A' && digit <= 'F') {
                        value |= static_cast<unsigned>(digit - 'A' + 10);
                    } else {
                        return fail("invalid unicode escape");
                    }
                }
                if (value < 0x80) {
                    out.push_back(static_cast<char>(value));
                } else if (value < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (value >> 6)));
                    out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (value >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                }
                break;
            }
            default:
                return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_number(Json& out) {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }
        while (position_ < input_.size() &&
               (std::isdigit(static_cast<unsigned char>(input_[position_])) != 0 ||
                input_[position_] == '.' || input_[position_] == 'e' ||
                input_[position_] == 'E' || input_[position_] == '+' ||
                input_[position_] == '-')) {
            ++position_;
        }
        if (position_ == start) {
            return fail("expected value");
        }
        const std::string text(input_.substr(start, position_ - start));
        char* end = nullptr;
        out.number = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') {
            return fail("invalid number");
        }
        out.type = Json::Type::Number;
        return true;
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::string error_;
};

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (char ch : text) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 32) {
                char buffer[8] = {};
                std::snprintf(buffer, sizeof(buffer), "\\u%04X",
                              static_cast<unsigned>(ch));
                out += buffer;
            } else {
                out.push_back(ch);
            }
            break;
        }
    }
    return out;
}

std::string dump(const Json& value) {
    switch (value.type) {
    case Json::Type::Null: return "null";
    case Json::Type::Bool: return value.boolean ? "true" : "false";
    case Json::Type::Number: {
        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "%.17g", value.number);
        return buffer;
    }
    case Json::Type::String: return "\"" + escape(value.text) + "\"";
    case Json::Type::Array: {
        std::string out = "[";
        for (std::size_t index = 0; index < value.array.size(); ++index) {
            if (index != 0) out += ",";
            out += dump(value.array[index]);
        }
        out += "]";
        return out;
    }
    case Json::Type::Object: {
        std::string out = "{";
        bool first = true;
        for (const auto& [key, item] : value.object) {
            if (!first) out += ",";
            first = false;
            out += "\"" + escape(key) + "\":" + dump(item);
        }
        out += "}";
        return out;
    }
    }
    return "null";
}

const Json* field(const Json& object, std::string_view key) {
    if (object.type != Json::Type::Object) {
        return nullptr;
    }
    const auto it = object.object.find(std::string(key));
    return it == object.object.end() ? nullptr : &it->second;
}

std::string string_field(const Json& object, std::string_view key,
                         std::string fallback = {}) {
    const Json* value = field(object, key);
    return value != nullptr && value->type == Json::Type::String ? value->text
                                                                : fallback;
}

double number_field(const Json& object, std::string_view key, double fallback) {
    const Json* value = field(object, key);
    return value != nullptr && value->type == Json::Type::Number ? value->number
                                                                : fallback;
}

bool bool_field(const Json& object, std::string_view key, bool fallback) {
    const Json* value = field(object, key);
    return value != nullptr && value->type == Json::Type::Bool ? value->boolean
                                                               : fallback;
}

std::string type_name(SettingType type) {
    switch (type) {
    case SettingType::Bool: return "bool";
    case SettingType::Int: return "int";
    case SettingType::Float: return "float";
    case SettingType::String: return "string";
    case SettingType::Enum: return "enum";
    case SettingType::Path: return "path";
    case SettingType::StringList: return "string_list";
    case SettingType::StringMap: return "string_map";
    }
    return "string";
}

SettingType parse_type(std::string_view text) {
    if (text == "bool") return SettingType::Bool;
    if (text == "int") return SettingType::Int;
    if (text == "float") return SettingType::Float;
    if (text == "enum") return SettingType::Enum;
    if (text == "path") return SettingType::Path;
    if (text == "string_list") return SettingType::StringList;
    if (text == "string_map") return SettingType::StringMap;
    return SettingType::String;
}

std::string origin_name(SettingOrigin origin) {
    switch (origin) {
    case SettingOrigin::User: return "user";
    case SettingOrigin::Project: return "project";
    case SettingOrigin::Default: return "default";
    }
    return "default";
}

SettingOrigin parse_origin(std::string_view text) {
    if (text == "user") return SettingOrigin::User;
    if (text == "project") return SettingOrigin::Project;
    return SettingOrigin::Default;
}

std::string scope_name(SettingScope scope) {
    return scope == SettingScope::Shared ? "shared" : "target";
}

SettingScope parse_scope(std::string_view text) {
    if (text == "shared") return SettingScope::Shared;
    if (text == "target") return SettingScope::Target;
    // An unknown scope must not make a VM setting global by accident.
    return SettingScope::Target;
}

Json value_json(const SettingValue& value) {
    Json out;
    if (const auto* boolean = std::get_if<bool>(&value)) {
        out.type = Json::Type::Bool;
        out.boolean = *boolean;
    } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        out.type = Json::Type::Number;
        out.number = static_cast<double>(*integer);
    } else if (const auto* number = std::get_if<double>(&value)) {
        out.type = Json::Type::Number;
        out.number = *number;
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        out.type = Json::Type::String;
        out.text = *text;
    } else if (const auto* list = std::get_if<std::vector<std::string>>(&value)) {
        out.type = Json::Type::Array;
        for (const std::string& item : *list) {
            Json string;
            string.type = Json::Type::String;
            string.text = item;
            out.array.push_back(std::move(string));
        }
    } else if (const auto* map =
                   std::get_if<std::map<std::string, std::string>>(&value)) {
        out.type = Json::Type::Object;
        for (const auto& [key, item] : *map) {
            Json string;
            string.type = Json::Type::String;
            string.text = item;
            out.object[key] = std::move(string);
        }
    }
    return out;
}

bool parse_value_for(const SettingDescriptor& descriptor, const Json& input,
                     SettingValue& out) {
    switch (descriptor.type) {
    case SettingType::Bool:
        if (input.type != Json::Type::Bool) return false;
        out = input.boolean;
        return true;
    case SettingType::Int:
        if (input.type != Json::Type::Number) return false;
        out = static_cast<std::int64_t>(std::llround(input.number));
        return true;
    case SettingType::Float:
        if (input.type != Json::Type::Number) return false;
        out = input.number;
        return true;
    case SettingType::String:
    case SettingType::Path:
        if (input.type != Json::Type::String) return false;
        out = input.text;
        return true;
    case SettingType::Enum:
        if (input.type != Json::Type::String) return false;
        out = input.text;
        return true;
    case SettingType::StringList: {
        if (input.type != Json::Type::Array) return false;
        std::vector<std::string> list;
        for (const Json& item : input.array) {
            if (item.type != Json::Type::String) return false;
            list.push_back(item.text);
        }
        out = std::move(list);
        return true;
    }
    case SettingType::StringMap: {
        if (input.type != Json::Type::Object) return false;
        std::map<std::string, std::string> map;
        for (const auto& [key, item] : input.object) {
            if (item.type != Json::Type::String) return false;
            map[key] = item.text;
        }
        out = std::move(map);
        return true;
    }
    }
    return false;
}

} // namespace

std::string schema_to_json(const SettingsSchema& schema) {
    Json root;
    root.type = Json::Type::Object;
    Json version;
    version.type = Json::Type::Number;
    version.number = schema.version;
    root.object["version"] = version;

    Json groups;
    groups.type = Json::Type::Array;
    for (const SettingsGroup& group : schema.groups) {
        Json item;
        item.type = Json::Type::Object;
        item.object["id"] = Json{Json::Type::String, false, 0.0, group.id, {}, {}};
        item.object["label"] =
            Json{Json::Type::String, false, 0.0, group.label, {}, {}};
        item.object["description"] =
            Json{Json::Type::String, false, 0.0, group.description, {}, {}};
        Json order;
        order.type = Json::Type::Number;
        order.number = group.order;
        item.object["order"] = order;
        groups.array.push_back(std::move(item));
    }
    root.object["groups"] = std::move(groups);

    Json settings;
    settings.type = Json::Type::Array;
    for (const SettingDescriptor& setting : schema.settings) {
        Json item;
        item.type = Json::Type::Object;
        item.object["id"] =
            Json{Json::Type::String, false, 0.0, setting.id, {}, {}};
        item.object["group"] =
            Json{Json::Type::String, false, 0.0, setting.group, {}, {}};
        item.object["label"] =
            Json{Json::Type::String, false, 0.0, setting.label, {}, {}};
        item.object["description"] =
            Json{Json::Type::String, false, 0.0, setting.description, {}, {}};
        item.object["type"] = Json{Json::Type::String, false, 0.0,
                                   type_name(setting.type), {}, {}};
        item.object["default"] =
            Json{Json::Type::String, false, 0.0, setting.default_json, {}, {}};
        Json min;
        min.type = Json::Type::Number;
        min.number = setting.min;
        item.object["min"] = min;
        Json max;
        max.type = Json::Type::Number;
        max.number = setting.max;
        item.object["max"] = max;
        Json step;
        step.type = Json::Type::Number;
        step.number = setting.step;
        item.object["step"] = step;
        item.object["unit"] =
            Json{Json::Type::String, false, 0.0, setting.unit, {}, {}};
        Json choices;
        choices.type = Json::Type::Array;
        for (const std::string& choice : setting.choices) {
            choices.array.push_back(
                Json{Json::Type::String, false, 0.0, choice, {}, {}});
        }
        item.object["choices"] = std::move(choices);
        Json range;
        range.type = Json::Type::Bool;
        range.boolean = setting.has_range;
        item.object["has_range"] = range;
        Json advanced;
        advanced.type = Json::Type::Bool;
        advanced.boolean = setting.advanced;
        item.object["advanced"] = advanced;
        Json secret;
        secret.type = Json::Type::Bool;
        secret.boolean = setting.secret;
        item.object["secret"] = secret;
        Json readonly;
        readonly.type = Json::Type::Bool;
        readonly.boolean = setting.read_only;
        item.object["read_only"] = readonly;
        Json restart;
        restart.type = Json::Type::Bool;
        restart.boolean = setting.requires_restart;
        item.object["requires_restart"] = restart;
        item.object["scope"] =
            Json{Json::Type::String, false, 0.0, scope_name(setting.scope), {}, {}};
        settings.array.push_back(std::move(item));
    }
    root.object["settings"] = std::move(settings);
    return dump(root);
}

bool schema_from_json(std::string_view json, SettingsSchema& schema,
                      std::string& error) {
    Json root;
    if (!Parser(json).parse(root, error)) {
        return false;
    }
    if (root.type != Json::Type::Object) {
        error = "schema root must be an object";
        return false;
    }
    schema = {};
    schema.version = static_cast<std::uint32_t>(
        number_field(root, "version", 1.0));
    if (const Json* groups = field(root, "groups");
        groups != nullptr && groups->type == Json::Type::Array) {
        for (const Json& item : groups->array) {
            if (item.type != Json::Type::Object) continue;
            SettingsGroup group;
            group.id = string_field(item, "id");
            group.label = string_field(item, "label");
            group.description = string_field(item, "description");
            group.order = static_cast<int>(number_field(item, "order", 0.0));
            schema.groups.push_back(std::move(group));
        }
    }
    if (const Json* settings = field(root, "settings");
        settings != nullptr && settings->type == Json::Type::Array) {
        for (const Json& item : settings->array) {
            if (item.type != Json::Type::Object) continue;
            SettingDescriptor setting;
            setting.id = string_field(item, "id");
            setting.group = string_field(item, "group");
            setting.label = string_field(item, "label");
            setting.description = string_field(item, "description");
            setting.type = parse_type(string_field(item, "type", "string"));
            setting.default_json = string_field(item, "default");
            setting.min = number_field(item, "min", 0.0);
            setting.max = number_field(item, "max", 0.0);
            setting.step = number_field(item, "step", 0.0);
            setting.unit = string_field(item, "unit");
            if (const Json* choices = field(item, "choices");
                choices != nullptr && choices->type == Json::Type::Array) {
                for (const Json& choice : choices->array) {
                    if (choice.type == Json::Type::String) {
                        setting.choices.push_back(choice.text);
                    }
                }
            }
            setting.has_range = bool_field(item, "has_range", false);
            setting.advanced = bool_field(item, "advanced", false);
            setting.secret = bool_field(item, "secret", false);
            setting.read_only = bool_field(item, "read_only", false);
            setting.requires_restart =
                bool_field(item, "requires_restart", false);
            setting.scope = parse_scope(string_field(item, "scope", "target"));
            schema.settings.push_back(std::move(setting));
        }
    }
    return true;
}

namespace {

// Appends a snapshot's values and origins to an object that will be written as
// a settings section. Shared by the flat snapshot form and document sections so
// both stay in one format.
void append_snapshot(Json& object, const SettingsSnapshot& snapshot) {
    Json values;
    values.type = Json::Type::Object;
    for (const auto& [id, value] : snapshot.values) {
        values.object[id] = value_json(value);
    }
    object.object["values"] = std::move(values);

    Json origins;
    origins.type = Json::Type::Object;
    for (const auto& [id, origin] : snapshot.origins) {
        origins.object[id] =
            Json{Json::Type::String, false, 0.0, origin_name(origin), {}, {}};
    }
    object.object["origins"] = std::move(origins);
}

// Reads `values`/`origins` from an object into a snapshot. Values the schema
// does not know are ignored. `require_values` is the flat-file contract: a
// snapshot without a values object is malformed, while a document section may
// be empty.
bool fill_snapshot(const Json& object, const SettingsSchema& schema,
                   SettingsSnapshot& snapshot, bool require_values,
                   std::string& error) {
    snapshot = {};
    const Json* values = field(object, "values");
    if (values == nullptr) {
        if (require_values) {
            error = "snapshot needs a values object";
            return false;
        }
    } else if (values->type != Json::Type::Object) {
        error = "snapshot values must be an object";
        return false;
    } else {
        for (const auto& [id, json_value] : values->object) {
            const SettingDescriptor* descriptor = nullptr;
            for (const SettingDescriptor& candidate : schema.settings) {
                if (candidate.id == id) {
                    descriptor = &candidate;
                    break;
                }
            }
            if (descriptor == nullptr) {
                continue;
            }
            SettingValue value;
            if (!parse_value_for(*descriptor, json_value, value)) {
                error = "bad value for " + id;
                return false;
            }
            snapshot.values[id] = std::move(value);
        }
    }
    if (const Json* origins = field(object, "origins");
        origins != nullptr && origins->type == Json::Type::Object) {
        for (const auto& [id, value] : origins->object) {
            if (value.type == Json::Type::String &&
                snapshot.values.count(id) != 0) {
                snapshot.origins[id] = parse_origin(value.text);
            }
        }
    }
    for (const auto& [id, value] : snapshot.values) {
        if (snapshot.origins.count(id) == 0) {
            snapshot.origins[id] = SettingOrigin::Default;
        }
    }
    return true;
}

Json section_json(const SettingsSnapshot& snapshot) {
    Json section;
    section.type = Json::Type::Object;
    append_snapshot(section, snapshot);
    return section;
}

bool write_json_file(const std::string& path, const std::string& json,
                     std::string& error) {
    std::error_code code;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), code);
        if (code) {
            error = "cannot create " + target.parent_path().string();
            return false;
        }
    }
    std::ofstream output(path);
    if (!output) {
        error = "cannot write " + path;
        return false;
    }
    output << json << '\n';
    if (!output) {
        error = "write failed for " + path;
        return false;
    }
    return true;
}

std::string read_json_file(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "cannot open " + path;
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

} // namespace

std::string snapshot_to_json(const SettingsSchema& schema,
                             const SettingsSnapshot& snapshot) {
    Json root;
    root.type = Json::Type::Object;
    Json version;
    version.type = Json::Type::Number;
    version.number = schema.version;
    root.object["version"] = version;
    append_snapshot(root, snapshot);
    return dump(root);
}

bool snapshot_from_json(std::string_view json, const SettingsSchema& schema,
                        SettingsSnapshot& snapshot, std::string& error) {
    Json root;
    if (!Parser(json).parse(root, error)) {
        return false;
    }
    if (root.type != Json::Type::Object) {
        error = "snapshot root must be an object";
        return false;
    }
    return fill_snapshot(root, schema, snapshot, true, error);
}

bool default_value(const SettingDescriptor& descriptor, SettingValue& value) {
    Json json;
    std::string error;
    if (!Parser(descriptor.default_json).parse(json, error)) {
        return false;
    }
    return parse_value_for(descriptor, json, value);
}

std::string document_to_json(const SettingsSchema& schema,
                             const SettingsDocument& document) {
    (void)schema;
    Json root;
    root.type = Json::Type::Object;
    Json version;
    version.type = Json::Type::Number;
    version.number = kSettingsDocumentVersion;
    root.object["version"] = version;
    root.object["shared"] = section_json(document.shared);
    Json targets;
    targets.type = Json::Type::Object;
    for (const auto& [target, section] : document.targets) {
        targets.object[target] = section_json(section);
    }
    root.object["targets"] = std::move(targets);
    return dump(root);
}

bool document_from_json(std::string_view json, const SettingsSchema& schema,
                        SettingsDocument& document, std::string& error) {
    Json root;
    if (!Parser(json).parse(root, error)) {
        return false;
    }
    if (root.type != Json::Type::Object) {
        error = "settings document root must be an object";
        return false;
    }
    document = {};
    document.version = static_cast<std::uint32_t>(
        number_field(root, "version", kSettingsDocumentVersion));

    const Json* shared = field(root, "shared");
    const Json* targets = field(root, "targets");
    const bool flat_values = field(root, "values") != nullptr;
    if (shared == nullptr && targets == nullptr) {
        if (!flat_values) {
            error = "settings document has no values";
            return false;
        }
        document.legacy_flat = true;
        return fill_snapshot(root, schema, document.shared, true, error);
    }
    if (flat_values) {
        error = "settings document mixes flat values with sections";
        return false;
    }
    if (shared != nullptr) {
        if (shared->type != Json::Type::Object) {
            error = "settings document shared section must be an object";
            return false;
        }
        if (!fill_snapshot(*shared, schema, document.shared, false, error)) {
            return false;
        }
    }
    if (targets != nullptr) {
        if (targets->type != Json::Type::Object) {
            error = "settings document targets must be an object";
            return false;
        }
        for (const auto& [target, section] : targets->object) {
            if (section.type != Json::Type::Object) {
                error = "target '" + target + "' section must be an object";
                return false;
            }
            SettingsSnapshot parsed;
            if (!fill_snapshot(section, schema, parsed, false, error)) {
                error = "target '" + target + "': " + error;
                return false;
            }
            document.targets[target] = std::move(parsed);
        }
    }
    return true;
}

bool load_document_file(const std::string& path, const SettingsSchema& schema,
                        SettingsDocument& document, std::string& error) {
    const std::string json = read_json_file(path, error);
    if (json.empty() && !error.empty()) {
        return false;
    }
    return document_from_json(json, schema, document, error);
}

bool save_document_file(const std::string& path, const SettingsSchema& schema,
                        const SettingsDocument& document, std::string& error) {
    return write_json_file(path, document_to_json(schema, document), error);
}

bool load_snapshot_file(const std::string& path, const SettingsSchema& schema,
                        SettingsSnapshot& snapshot, std::string& error) {
    const std::string json = read_json_file(path, error);
    if (json.empty() && !error.empty()) {
        return false;
    }
    return snapshot_from_json(json, schema, snapshot, error);
}

bool save_snapshot_file(const std::string& path, const SettingsSchema& schema,
                        const SettingsSnapshot& snapshot, std::string& error) {
    return write_json_file(path, snapshot_to_json(schema, snapshot), error);
}

std::string discover_project_file(const std::string& loaded_path) {
    if (loaded_path.empty()) {
        return {};
    }
    std::error_code code;
    std::filesystem::path path = std::filesystem::absolute(loaded_path, code);
    if (code) {
        path = std::filesystem::path(loaded_path);
    }
    if (!std::filesystem::is_directory(path, code)) {
        path = path.parent_path();
    }
    while (!path.empty()) {
        const std::filesystem::path candidate =
            path / ".krdbg" / "settings.json";
        if (std::filesystem::is_regular_file(candidate, code)) {
            return candidate.string();
        }
        const std::filesystem::path parent = path.parent_path();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return {};
}

} // namespace krdbg::settings_json
