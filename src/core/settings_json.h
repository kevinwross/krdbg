#pragma once

#include <map>
#include <string>
#include <string_view>

#include "krdbg/settings.h"

namespace krdbg {

// A persisted settings document: one shared section plus one section per stable
// target id ("kr32", "kr16", "kr216", "kr232"). A user document stores the
// values the user chose; a project document stores that checkout's overrides in
// the same shape. The origins inside a section are informational: the layer a
// document is loaded as decides the badge, not the string in the file.
struct SettingsDocument {
    std::uint32_t version = 2;
    SettingsSnapshot shared;
    std::map<std::string, SettingsSnapshot> targets;
    // True when the file was the pre-section flat form. The host rewrites it in
    // document form on the next save.
    bool legacy_flat = false;
};

inline constexpr std::uint32_t kSettingsDocumentVersion = 2;

} // namespace krdbg

namespace krdbg::settings_json {

// Canonical JSON for the schema and a snapshot. The schema version is emitted
// as `version`; unknown fields are ignored on read so newer hosts can add them.
// A descriptor's `scope` is emitted as "shared" or "target".
std::string schema_to_json(const SettingsSchema& schema);
bool schema_from_json(std::string_view json, SettingsSchema& schema,
                      std::string& error);

std::string snapshot_to_json(const SettingsSchema& schema,
                             const SettingsSnapshot& snapshot);
bool snapshot_from_json(std::string_view json, const SettingsSchema& schema,
                        SettingsSnapshot& snapshot, std::string& error);

// Parses a descriptor's canonical default_json into its typed value. The
// aggregator calls this to show a real default for a setting nobody set.
bool default_value(const SettingDescriptor& descriptor, SettingValue& value);

// The document form described above. document_from_json accepts a version-1
// flat `{version, values, origins}` file as the shared section (legacy_flat is
// then true); an unknown value id is ignored, an unknown target section is kept
// as loaded.
std::string document_to_json(const SettingsSchema& schema,
                             const SettingsDocument& document);
bool document_from_json(std::string_view json, const SettingsSchema& schema,
                        SettingsDocument& document, std::string& error);

bool load_document_file(const std::string& path, const SettingsSchema& schema,
                        SettingsDocument& document, std::string& error);
bool save_document_file(const std::string& path, const SettingsSchema& schema,
                        const SettingsDocument& document, std::string& error);

// Layer files are host-owned: `.krdbg/settings.json` for the project and the
// user document older hosts already wrote.
bool load_snapshot_file(const std::string& path, const SettingsSchema& schema,
                        SettingsSnapshot& snapshot, std::string& error);
bool save_snapshot_file(const std::string& path, const SettingsSchema& schema,
                        const SettingsSnapshot& snapshot, std::string& error);

// The project settings file for a loaded source or image: the first
// `.krdbg/settings.json` found in an ancestor of the path (its directory when
// the path is a directory). Empty when nothing was found or the path is empty.
std::string discover_project_file(const std::string& loaded_path);

} // namespace krdbg::settings_json
