#include "settings_host.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "settings_validation.h"
#include "tool_settings.h"

namespace krdbg {
namespace {

std::string qualify(const std::string& provider, const std::string& id) {
    const std::string prefix = provider + ".";
    if (id.rfind(prefix, 0) == 0) {
        return id;
    }
    return prefix + id;
}

bool provider_for(const std::vector<ISettingsProvider*>& providers,
                  const std::string& qualified_id,
                  ISettingsProvider*& provider, std::string& local_id) {
    std::size_t best = 0;
    for (ISettingsProvider* candidate : providers) {
        if (candidate == nullptr) {
            continue;
        }
        const std::string prefix = candidate->id() + ".";
        if (qualified_id.rfind(prefix, 0) == 0 &&
            prefix.size() > best) {
            best = prefix.size();
            provider = candidate;
            local_id = qualified_id.substr(prefix.size());
        }
    }
    return best != 0;
}

SettingsSnapshot local_values(const SettingsSnapshot& values,
                              ISettingsProvider& provider) {
    SettingsSnapshot local;
    const std::string prefix = provider.id() + ".";
    for (const auto& [id, value] : values.values) {
        if (id.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string local_id = id.substr(prefix.size());
        local.values[local_id] = value;
        if (const auto origin = values.origins.find(id);
            origin != values.origins.end()) {
            local.origins[local_id] = origin->second;
        }
    }
    return local;
}

SettingOrigin layer_origin(SettingsLayer layer) {
    return layer == SettingsLayer::User ? SettingOrigin::User
                                        : SettingOrigin::Project;
}

} // namespace

SettingsAggregator::SettingsAggregator(
    std::vector<ISettingsProvider*> providers)
    : providers_(std::move(providers)) {
    std::string error;
    (void)refresh(error);
}

void SettingsAggregator::set_providers(
    std::vector<ISettingsProvider*> providers) {
    providers_ = std::move(providers);
    std::string error;
    (void)refresh(error);
}

void SettingsAggregator::set_active_target(std::string target_id) {
    active_target_ = std::move(target_id);
}

bool SettingsAggregator::default_value(const SettingDescriptor& descriptor,
                                       SettingValue& value,
                                       std::string& error) const {
    if (const std::optional<SettingValue> target =
            target_default(active_target_, descriptor.id);
        target.has_value()) {
        value = *target;
        if (!settings_validation::validate(descriptor, value, error)) {
            error = "target default for '" + descriptor.id + "': " + error;
            return false;
        }
        return true;
    }
    if (!settings_json::default_value(descriptor, value)) {
        error = "descriptor default for '" + descriptor.id +
                "' is not canonical JSON";
        return false;
    }
    if (!settings_validation::validate(descriptor, value, error)) {
        error = "descriptor default for '" + descriptor.id + "': " + error;
        return false;
    }
    return true;
}

bool SettingsAggregator::refresh(std::string& error) {
    const SettingsSnapshot previous = values_;
    schema_ = {};
    values_ = {};
    error_.clear();

    std::set<std::string> group_ids;
    std::set<std::string> setting_ids;
    for (ISettingsProvider* provider : providers_) {
        if (provider == nullptr) {
            continue;
        }
        const std::string provider_id = provider->id();
        if (provider_id.empty() ||
            provider_id.find('.') != std::string::npos) {
            error = "settings provider id must be a simple non-empty name";
            error_ = error;
            return false;
        }
        const SettingsSchema local = provider->schema();
        for (const SettingsGroup& group : local.groups) {
            SettingsGroup qualified = group;
            qualified.id = qualify(provider_id, group.id);
            if (group_ids.insert(qualified.id).second) {
                schema_.groups.push_back(std::move(qualified));
            }
        }
        for (const SettingDescriptor& setting : local.settings) {
            SettingDescriptor qualified = setting;
            qualified.id = qualify(provider_id, setting.id);
            qualified.group = qualify(
                provider_id, setting.group.empty() ? provider_id : setting.group);
            if (!setting_ids.insert(qualified.id).second) {
                error = "duplicate setting id '" + qualified.id + "'";
                error_ = error;
                return false;
            }
            if (group_ids.find(qualified.group) == group_ids.end()) {
                SettingsGroup group;
                group.id = qualified.group;
                group.label = qualified.group;
                group.order = 1000;
                group_ids.insert(group.id);
                schema_.groups.push_back(std::move(group));
            }
            schema_.settings.push_back(std::move(qualified));
        }
        const SettingsSnapshot local_values_snapshot = provider->values();
        for (const auto& [id, value] : local_values_snapshot.values) {
            const std::string qualified_id = qualify(provider_id, id);
            if (values_.values.find(qualified_id) != values_.values.end()) {
                error = "duplicate setting value '" + qualified_id + "'";
                error_ = error;
                return false;
            }
            values_.values[qualified_id] = value;
        }
        schema_.version = std::max(schema_.version, local.version);
    }

    std::stable_sort(schema_.groups.begin(), schema_.groups.end(),
                     [](const SettingsGroup& left, const SettingsGroup& right) {
                         if (left.order != right.order) {
                             return left.order < right.order;
                         }
                         return left.id < right.id;
                     });
    std::stable_sort(schema_.settings.begin(), schema_.settings.end(),
                     [](const SettingDescriptor& left,
                        const SettingDescriptor& right) {
                         if (left.group != right.group) {
                             return left.group < right.group;
                         }
                         return left.id < right.id;
                     });

    // Every setting the dialog can show has a value: a provider's current one,
    // or the canonical default (the target's default where it has one). A value
    // with no recorded origin is a code default.
    for (const SettingDescriptor& descriptor : schema_.settings) {
        SettingOrigin origin = SettingOrigin::Default;
        if (const auto recorded = resolved_origins_.find(descriptor.id);
            recorded != resolved_origins_.end()) {
            origin = recorded->second;
        } else if (values_.values.count(descriptor.id) != 0) {
            const auto previous_origin = previous.origins.find(descriptor.id);
            if (previous_origin != previous.origins.end()) {
                origin = previous_origin->second;
            }
        }
        if (values_.values.count(descriptor.id) == 0) {
            SettingValue value;
            if (!default_value(descriptor, value, error)) {
                error_ = error;
                return false;
            }
            values_.values[descriptor.id] = std::move(value);
        }
        values_.origins[descriptor.id] = origin;
    }

    if (schema_.version == 0) {
        schema_.version = 1;
    }
    return true;
}

bool SettingsAggregator::apply_resolved(const SettingsSnapshot& values,
                                        SettingsApplyResult& result) {
    if (!error_.empty()) {
        result.ok = false;
        result.errors[""] = error_;
        return false;
    }

    std::map<ISettingsProvider*, SettingsSnapshot> local;
    for (const auto& [id, value] : values.values) {
        ISettingsProvider* provider = nullptr;
        std::string local_id;
        if (!provider_for(providers_, id, provider, local_id)) {
            result.ok = false;
            result.errors[id] = "no provider owns this setting";
            continue;
        }
        const auto descriptor = std::find_if(
            schema_.settings.begin(), schema_.settings.end(),
            [&id](const SettingDescriptor& candidate) {
                return candidate.id == id;
            });
        if (descriptor == schema_.settings.end()) {
            result.ok = false;
            result.errors[id] = "unknown setting";
            continue;
        }
        std::string error;
        if (!settings_validation::validate(*descriptor, value, error)) {
            result.ok = false;
            result.errors[id] = error;
            continue;
        }
        local[provider].values[local_id] = value;
        local[provider].origins[local_id] = SettingOrigin::Default;
    }
    if (!result.ok) {
        return false;
    }

    const SettingsSnapshot before = values_;
    for (const auto& [provider, snapshot] : local) {
        SettingsApplyResult applied = provider->apply(snapshot);
        if (!applied.ok) {
            result.ok = false;
            for (const auto& [id, message] : applied.errors) {
                result.errors[provider->id() + "." + id] = message;
            }
            break;
        }
        result.reload_required =
            result.reload_required || applied.reload_required;
    }
    if (!result.ok) {
        for (ISettingsProvider* provider : providers_) {
            if (provider == nullptr) {
                continue;
            }
            (void)provider->apply(local_values(before, *provider));
        }
        return false;
    }
    return true;
}

SettingsApplyResult SettingsAggregator::apply(const SettingsSnapshot& values,
                                              SettingsLayer layer) {
    SettingsApplyResult result;
    result.written_to = layer;
    if (!error_.empty()) {
        result.ok = false;
        result.errors[""] = error_;
        return result;
    }
    // An explicit origin on a value wins: the surface marks the fields the user
    // edited, and leaves untouched defaults at Default so they are not persisted
    // as if the user had chosen them.
    std::map<std::string, SettingOrigin> origins = [&] {
        std::map<std::string, SettingOrigin> out;
        for (const auto& [id, value] : values.values) {
            (void)value;
            const auto found = values.origins.find(id);
            out[id] = found == values.origins.end() ? layer_origin(layer)
                                                    : found->second;
        }
        return out;
    }();
    // The dialog applies its whole snapshot, so a field the user cleared is
    // absent from it. Reset a previously set field to its canonical default so
    // the layer override is removed instead of keeping the old live value.
    SettingsSnapshot effective = values;
    for (const SettingDescriptor& descriptor : schema_.settings) {
        if (values.values.count(descriptor.id) != 0) {
            continue;
        }
        const auto recorded = resolved_origins_.find(descriptor.id);
        if (recorded == resolved_origins_.end() ||
            recorded->second == SettingOrigin::Default) {
            continue;
        }
        SettingValue fallback;
        std::string default_error;
        if (!default_value(descriptor, fallback, default_error)) {
            continue;
        }
        effective.values[descriptor.id] = std::move(fallback);
        origins[descriptor.id] = SettingOrigin::Default;
    }
    if (!apply_resolved(effective, result)) {
        return result;
    }
    const std::map<std::string, SettingOrigin> before_origins = resolved_origins_;
    for (const auto& [id, origin] : origins) {
        resolved_origins_[id] = origin;
    }
    std::string refresh_error;
    if (!refresh(refresh_error)) {
        resolved_origins_ = before_origins;
        result.ok = false;
        result.errors[""] = refresh_error;
    }
    return result;
}

SettingsApplyResult SettingsAggregator::apply_layer_documents(
    const SettingsDocument* user, const SettingsDocument* project) {
    SettingsApplyResult result;
    if (!error_.empty()) {
        result.ok = false;
        result.errors[""] = error_;
        return result;
    }

    const auto descriptor_for =
        [this](const std::string& id) -> const SettingDescriptor* {
        for (const SettingDescriptor& descriptor : schema_.settings) {
            if (descriptor.id == id) {
                return &descriptor;
            }
        }
        return nullptr;
    };

    SettingsSnapshot resolved;
    for (const SettingDescriptor& descriptor : schema_.settings) {
        SettingValue value;
        std::string error;
        if (!default_value(descriptor, value, error)) {
            result.ok = false;
            result.errors[descriptor.id] = error;
            return result;
        }
        resolved.values[descriptor.id] = std::move(value);
        resolved.origins[descriptor.id] = SettingOrigin::Default;
    }
    const auto overlay = [&](const SettingsSnapshot& section,
                             SettingOrigin origin) {
        for (const auto& [id, value] : section.values) {
            const SettingDescriptor* descriptor = descriptor_for(id);
            if (descriptor == nullptr) {
                continue;  // unknown id: ignored, as on the flat file
            }
            if (descriptor->read_only) {
                continue;  // a target capability is not a user setting
            }
            resolved.values[id] = value;
            resolved.origins[id] = origin;
        }
    };
    if (user != nullptr) {
        overlay(user->shared, SettingOrigin::User);
        if (!active_target_.empty()) {
            if (const auto section = user->targets.find(active_target_);
                section != user->targets.end()) {
                overlay(section->second, SettingOrigin::User);
            }
        }
    }
    if (project != nullptr) {
        overlay(project->shared, SettingOrigin::Project);
        if (!active_target_.empty()) {
            if (const auto section = project->targets.find(active_target_);
                section != project->targets.end()) {
                overlay(section->second, SettingOrigin::Project);
            }
        }
    }
    for (const auto& [id, value] : resolved.values) {
        const SettingDescriptor* descriptor = descriptor_for(id);
        if (descriptor == nullptr) {
            continue;
        }
        std::string error;
        if (!settings_validation::validate(*descriptor, value, error)) {
            result.ok = false;
            result.errors[id] = error;
        }
    }
    if (!result.ok) {
        return result;
    }

    const SettingsSnapshot before = values_;
    const std::map<std::string, SettingOrigin> before_origins = resolved_origins_;
    const bool applied = apply_resolved(resolved, result);
    if (!applied) {
        return result;
    }
    resolved_origins_ = resolved.origins;
    std::string refresh_error;
    if (!refresh(refresh_error)) {
        resolved_origins_ = before_origins;
        for (ISettingsProvider* provider : providers_) {
            if (provider == nullptr) {
                continue;
            }
            (void)provider->apply(local_values(before, *provider));
        }
        result.ok = false;
        result.errors[""] = refresh_error;
    }
    return result;
}

} // namespace krdbg
