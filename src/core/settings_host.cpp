#include "settings_host.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "settings_validation.h"

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

bool SettingsAggregator::refresh(std::string& error) {
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
            const auto origin = local_values_snapshot.origins.find(id);
            values_.origins[qualified_id] =
                origin == local_values_snapshot.origins.end()
                    ? SettingOrigin::Default
                    : origin->second;
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
    if (schema_.version == 0) {
        schema_.version = 1;
    }
    return true;
}

SettingsApplyResult SettingsAggregator::apply(
    const SettingsSnapshot& values, SettingsLayer layer) {
    SettingsApplyResult result;
    result.written_to = layer;
    if (!error_.empty()) {
        result.ok = false;
        result.errors[""] = error_;
        return result;
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
        return result;
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
        return result;
    }

    std::string refresh_error;
    if (!refresh(refresh_error)) {
        result.ok = false;
        result.errors[""] = refresh_error;
        return result;
    }
    return result;
}

} // namespace krdbg
