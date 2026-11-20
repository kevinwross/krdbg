#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "krdbg/settings.h"

namespace krdbg::settings_validation {

// Validates one value against its descriptor. The provider and the host both
// call this so the dialog, the aggregator and direct providers agree.
inline bool validate(const SettingDescriptor& descriptor,
                     const SettingValue& value, std::string& error) {
    switch (descriptor.type) {
    case SettingType::Bool:
        if (!std::holds_alternative<bool>(value)) {
            error = "expected a boolean";
            return false;
        }
        return true;
    case SettingType::Int: {
        if (!std::holds_alternative<std::int64_t>(value)) {
            error = "expected an integer";
            return false;
        }
        const std::int64_t number = std::get<std::int64_t>(value);
        if (descriptor.has_range &&
            (number < static_cast<std::int64_t>(descriptor.min) ||
             number > static_cast<std::int64_t>(descriptor.max))) {
            error = "outside the allowed range";
            return false;
        }
        if (descriptor.has_range && descriptor.step > 0.0) {
            const double offset =
                static_cast<double>(number) - descriptor.min;
            const double steps = offset / descriptor.step;
            if (std::fabs(steps - std::round(steps)) > 1e-9) {
                error = "does not match the step";
                return false;
            }
        }
        return true;
    }
    case SettingType::Float: {
        double number = 0.0;
        if (const auto* real = std::get_if<double>(&value)) {
            number = *real;
        } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            number = static_cast<double>(*integer);
        } else {
            error = "expected a number";
            return false;
        }
        if (descriptor.has_range &&
            (number < descriptor.min || number > descriptor.max)) {
            error = "outside the allowed range";
            return false;
        }
        return true;
    }
    case SettingType::String:
    case SettingType::Path:
        if (!std::holds_alternative<std::string>(value)) {
            error = "expected text";
            return false;
        }
        return true;
    case SettingType::Enum: {
        const auto* text = std::get_if<std::string>(&value);
        if (text == nullptr) {
            error = "expected one of the listed values";
            return false;
        }
        for (const std::string& choice : descriptor.choices) {
            if (*text == choice) {
                return true;
            }
        }
        error = "not one of the listed values";
        return false;
    }
    case SettingType::StringList:
        if (!std::holds_alternative<std::vector<std::string>>(value)) {
            error = "expected a list of strings";
            return false;
        }
        return true;
    case SettingType::StringMap:
        if (!std::holds_alternative<std::map<std::string, std::string>>(value)) {
            error = "expected a string map";
            return false;
        }
        return true;
    }
    error = "unknown setting type";
    return false;
}

} // namespace krdbg::settings_validation
