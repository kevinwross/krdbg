#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace krdbg::host {

struct ToolchainPaths {
    std::filesystem::path prefix;
    std::filesystem::path krc;
    std::filesystem::path krld;
    std::filesystem::path sysroot;
};

// Finds <prefix>/bin/krc and <prefix>/bin/krld plus the installed sysroot.
// This is the installed KRCompiler package layout; no build tree is inspected.
bool discover_toolchain(const std::filesystem::path& prefix,
                        ToolchainPaths& paths, std::string& error);

struct BuildOptions {
    std::string target = "kr32";
    std::string optimization = "-O0";
    std::vector<std::string> defines;
    std::vector<std::string> include_paths;
    std::string base = "0x10000";
    std::string entry = "_start";
    bool map = false;
    bool strip = false;
    bool gc_sections = false;
    std::string source_root;
};

struct BuildResult {
    bool ok = false;
    std::filesystem::path image;
    std::string log;
    std::string error;
};

class BuildChain {
public:
    explicit BuildChain(ToolchainPaths paths);

    BuildResult build(const std::filesystem::path& source,
                      const BuildOptions& options) const;

    const ToolchainPaths& paths() const { return paths_; }

private:
    ToolchainPaths paths_;
};

} // namespace krdbg::host
