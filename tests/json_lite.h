#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A tiny JSON reader for the committed krc --dump-symbols --json golden. It
// covers objects, arrays, strings, integers, booleans and null; it is test-only
// and deliberately not a general-purpose parser.
namespace test_json {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    long long number = 0;
    std::string text;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    [[nodiscard]] const Value& operator[](std::string_view key) const {
        const auto found = object.find(std::string(key));
        static const Value null_value;
        return found == object.end() ? null_value : found->second;
    }

    [[nodiscard]] bool is_number() const { return kind == Kind::Number; }
    [[nodiscard]] bool is_string() const { return kind == Kind::String; }
    [[nodiscard]] bool is_array() const { return kind == Kind::Array; }
    [[nodiscard]] bool is_object() const { return kind == Kind::Object; }
};

namespace detail {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool parse(Value& out, std::string& error) {
        if (!parse_value(out, error)) {
            return false;
        }
        skip();
        if (position_ != text_.size()) {
            return fail("trailing data", error);
        }
        return true;
    }

private:
    bool fail(const char* message, std::string& error) {
        error = std::string(message) + " at offset " + std::to_string(position_);
        return false;
    }

    void skip() {
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    bool parse_value(Value& out, std::string& error) {
        skip();
        if (position_ >= text_.size()) {
            return fail("unexpected end of input", error);
        }
        switch (text_[position_]) {
        case '{': return parse_object(out, error);
        case '[': return parse_array(out, error);
        case '"':
            out.kind = Value::Kind::String;
            return parse_string(out.text, error);
        case 't':
            if (text_.substr(position_, 4) != "true") {
                return fail("bad literal", error);
            }
            position_ += 4;
            out.kind = Value::Kind::Bool;
            out.boolean = true;
            return true;
        case 'f':
            if (text_.substr(position_, 5) != "false") {
                return fail("bad literal", error);
            }
            position_ += 5;
            out.kind = Value::Kind::Bool;
            out.boolean = false;
            return true;
        case 'n':
            if (text_.substr(position_, 4) != "null") {
                return fail("bad literal", error);
            }
            position_ += 4;
            out.kind = Value::Kind::Null;
            return true;
        default:
            return parse_number(out, error);
        }
    }

    bool parse_number(Value& out, std::string& error) {
        const std::size_t start = position_;
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
        }
        const std::size_t digits = position_;
        while (position_ < text_.size() && text_[position_] >= '0' &&
               text_[position_] <= '9') {
            ++position_;
        }
        if (position_ == digits) {
            return fail("expected a number", error);
        }
        if (position_ < text_.size() &&
            (text_[position_] == '.' || text_[position_] == 'e' ||
             text_[position_] == 'E')) {
            return fail("fractional numbers are not used by the KDBG dump", error);
        }
        out.kind = Value::Kind::Number;
        out.number = std::stoll(std::string(text_.substr(start, position_ - start)));
        return true;
    }

    bool parse_string(std::string& out, std::string& error) {
        if (text_[position_] != '"') {
            return fail("expected a string", error);
        }
        ++position_;
        out.clear();
        while (position_ < text_.size()) {
            const char ch = text_[position_++];
            if (ch == '"') {
                return true;
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (position_ >= text_.size()) {
                return fail("unterminated escape", error);
            }
            const char escaped = text_[position_++];
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
                if (position_ + 4 > text_.size()) {
                    return fail("short unicode escape", error);
                }
                std::uint32_t code = 0;
                for (int index = 0; index < 4; ++index) {
                    const char digit = text_[position_++];
                    code <<= 4;
                    if (digit >= '0' && digit <= '9') {
                        code |= static_cast<std::uint32_t>(digit - '0');
                    } else if (digit >= 'a' && digit <= 'f') {
                        code |= static_cast<std::uint32_t>(digit - 'a' + 10);
                    } else if (digit >= 'A' && digit <= 'F') {
                        code |= static_cast<std::uint32_t>(digit - 'A' + 10);
                    } else {
                        return fail("bad unicode escape", error);
                    }
                }
                if (code < 0x80U) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800U) {
                    out.push_back(static_cast<char>(0xC0U | (code >> 6)));
                    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
                } else {
                    out.push_back(static_cast<char>(0xE0U | (code >> 12)));
                    out.push_back(static_cast<char>(0x80U | ((code >> 6) & 0x3FU)));
                    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
                }
                break;
            }
            default:
                return fail("bad escape", error);
            }
        }
        return fail("unterminated string", error);
    }

    bool parse_object(Value& out, std::string& error) {
        out = Value{};
        out.kind = Value::Kind::Object;
        ++position_;  // '{'
        skip();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            return true;
        }
        while (true) {
            std::string key;
            if (position_ >= text_.size()) {
                return fail("unterminated object", error);
            }
            if (!parse_string(key, error)) {
                return false;
            }
            skip();
            if (position_ >= text_.size() || text_[position_] != ':') {
                return fail("expected ':'", error);
            }
            ++position_;
            Value value;
            if (!parse_value(value, error)) {
                return false;
            }
            out.object.emplace(std::move(key), std::move(value));
            skip();
            if (position_ >= text_.size()) {
                return fail("unterminated object", error);
            }
            if (text_[position_] == '}') {
                ++position_;
                return true;
            }
            if (text_[position_] != ',') {
                return fail("expected ',' or '}'", error);
            }
            ++position_;
            skip();
        }
    }

    bool parse_array(Value& out, std::string& error) {
        out = Value{};
        out.kind = Value::Kind::Array;
        ++position_;  // '['
        skip();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return true;
        }
        while (true) {
            Value value;
            if (!parse_value(value, error)) {
                return false;
            }
            out.array.push_back(std::move(value));
            skip();
            if (position_ >= text_.size()) {
                return fail("unterminated array", error);
            }
            if (text_[position_] == ']') {
                ++position_;
                return true;
            }
            if (text_[position_] != ',') {
                return fail("expected ',' or ']'", error);
            }
            ++position_;
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

} // namespace detail

inline bool parse(std::string_view text, Value& out, std::string& error) {
    detail::Parser parser(text);
    return parser.parse(out, error);
}

} // namespace test_json
