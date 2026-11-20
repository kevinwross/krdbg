#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "settings_host.h"
#include "settings_json.h"
#include "tool_settings.h"

namespace {

void usage() {
    std::printf(
        "usage: krdbg_settings --describe-settings | --settings <file.json>\n"
        "  --describe-settings  print the aggregated compiler/linker/assembler/\n"
        "                       debug schema as JSON\n"
        "  --settings FILE      load a JSON values file, apply it, and print the\n"
        "                       resulting values as JSON\n");
}

} // namespace

int main(int argc, char** argv) {
    krdbg::CompilerSettingsProvider compiler;
    krdbg::LinkerSettingsProvider linker;
    krdbg::AssemblerSettingsProvider assembler;
    krdbg::DebugSettingsProvider debug;
    krdbg::SettingsAggregator aggregator(
        {&compiler, &linker, &assembler, &debug});
    const std::string_view argument = argc >= 2 ? argv[1] : "";

    if (argc == 2 && argument == "--describe-settings") {
        std::cout << krdbg::settings_json::schema_to_json(aggregator.schema())
                  << '\n';
        return 0;
    }
    if (argument == "--settings" && argc == 3) {
        std::string error;
        krdbg::SettingsSnapshot values;
        if (!krdbg::settings_json::load_snapshot_file(argv[2],
                                                      aggregator.schema(),
                                                      values, error)) {
            std::fprintf(stderr, "krdbg_settings: %s\n", error.c_str());
            return 1;
        }
        const krdbg::SettingsApplyResult result =
            aggregator.apply(values, krdbg::SettingsLayer::User);
        if (!result.ok) {
            for (const auto& [id, message] : result.errors) {
                std::fprintf(stderr, "krdbg_settings: %s: %s\n", id.c_str(),
                             message.c_str());
            }
            return 1;
        }
        std::cout << krdbg::settings_json::snapshot_to_json(
                         aggregator.schema(), aggregator.values())
                  << '\n';
        return 0;
    }
    usage();
    return 2;
}
