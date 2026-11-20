#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace krdbg::host {

struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

// Runs one executable without a shell, capturing stdout and stderr together.
// `working_directory` is empty to inherit the host's current directory.
ProcessResult run_process(const std::filesystem::path& executable,
                          const std::vector<std::string>& arguments,
                          const std::filesystem::path& working_directory = {});

} // namespace krdbg::host
