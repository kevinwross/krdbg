#include "build_chain.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "krc/target/target.h"
#include "process.h"

namespace krdbg::host {
namespace {

std::filesystem::path executable(const std::filesystem::path& directory,
                                 const std::string& name) {
#if defined(_WIN32)
    return directory / (name + ".exe");
#else
    return directory / name;
#endif
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

void append(std::string& log, const std::string& text) {
    if (!text.empty()) {
        log += text;
        if (log.back() != '\n') {
            log.push_back('\n');
        }
    }
}

} // namespace

bool discover_toolchain(const std::filesystem::path& prefix,
                        ToolchainPaths& paths, std::string& error) {
    if (prefix.empty()) {
        error = "toolchain prefix is empty";
        return false;
    }
    paths.prefix = prefix;
    paths.krc = executable(prefix / "bin", "krc");
    paths.krld = executable(prefix / "bin", "krld");
    paths.sysroot = prefix / "share" / "krc" / "sysroot";
    if (!std::filesystem::exists(paths.krc)) {
        error = "krc is not installed under " + paths.krc.string();
        return false;
    }
    if (!std::filesystem::exists(paths.krld)) {
        error = "krld is not installed under " + paths.krld.string();
        return false;
    }
    if (!std::filesystem::exists(paths.sysroot)) {
        error = "the target sysroot is not installed under " +
                paths.sysroot.string();
        return false;
    }
    return true;
}

BuildChain::BuildChain(ToolchainPaths paths) : paths_(std::move(paths)) {}

BuildResult BuildChain::build(const std::filesystem::path& source,
                              const BuildOptions& options) const {
    BuildResult result;
    const std::string extension = lower(source.extension().string());
    const bool assembly = extension == ".s";
    if (!assembly && extension != ".c" && extension != ".cpp" &&
        extension != ".cc" && extension != ".cxx") {
        result.error = "unsupported source type '" + extension + "'";
        return result;
    }
    if (options.target != "kr32" && options.target != "kr16" &&
        options.target != "kr232") {
        result.error = "unsupported target '" + options.target + "'";
        return result;
    }

    std::error_code code;
    const std::filesystem::path output_directory =
        std::filesystem::current_path(code) / "build" / "krdbg-host";
    if (code) {
        result.error = "cannot resolve the build directory";
        return result;
    }
    std::filesystem::create_directories(output_directory, code);
    if (code) {
        result.error = "cannot create " + output_directory.string();
        return result;
    }

    const std::string stem = source.stem().string();
    // The image suffix comes from the compiler's one table, so a target added
    // there produces the right file name here without another branch.
    const auto target_id = krc::target::parse_target(options.target);
    const std::string image_suffix =
        target_id.has_value()
            ? std::string(krc::target::image_extension(*target_id))
            : std::string(".bin");
    const std::filesystem::path image = output_directory / (stem + image_suffix);
    const std::filesystem::path object = output_directory / (stem + ".kro");

    std::vector<std::string> common = {
        "--target=" + options.target,
        "--sysroot=" + paths_.sysroot.string(),
        options.optimization,
    };
    for (const std::string& define : options.defines) {
        common.push_back("-D" + define);
    }
    for (const std::string& include : options.include_paths) {
        common.push_back("-I" + include);
    }

    std::vector<std::string> runtime;
    if (options.target == "kr232") {
        // The startup is assembled here because the prefix ships its source, the
        // same way the kr16 branch does.
        const std::filesystem::path crt_object =
            output_directory / (stem + "-crt0.kro");
        std::vector<std::string> crt = common;
        crt.push_back("-c");
        crt.push_back((paths_.sysroot / "kr232" / "lib" / "crt0.s").string());
        crt.push_back("-o");
        crt.push_back(crt_object.string());
        const ProcessResult crt_compiled = run_process(paths_.krc, crt);
        append(result.log, crt_compiled.output);
        if (crt_compiled.exit_code != 0) {
            result.error = "krc failed to assemble the kr232 startup";
            return result;
        }
        runtime.push_back(crt_object.string());
        // The packages the prefix installs beside the startup: a program that
        // includes <kr232/cons.h> and its neighbours needs these objects, and a
        // chain that linked only the startup reported every such program as an
        // undefined symbol. Every object in the directory is taken, so a package
        // added to the prefix needs no change here.
        const std::filesystem::path package_directory =
            paths_.sysroot / "kr232" / "lib";
        std::error_code ignored;
        std::vector<std::filesystem::path> packages;
        for (const auto& entry :
             std::filesystem::directory_iterator(package_directory, ignored)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".kro") {
                continue;
            }
            packages.push_back(entry.path());
        }
        std::sort(packages.begin(), packages.end());
        for (const std::filesystem::path& package : packages) {
            runtime.push_back(package.string());
        }
    }
    if (options.target == "kr32") {
        runtime = {
            (paths_.sysroot / "kr32" / "lib" / "crt0.s").string(),
            (paths_.sysroot / "kr32" / "lib" / "fileio.s").string(),
            (paths_.sysroot / "kr32" / "lib" / "eh.s").string(),
            (paths_.sysroot / "kr32" / "lib" / "libkrcrt.kro").string(),
        };
    }

    if (options.target == "kr16") {
        if (assembly) {
            std::vector<std::string> assemble = common;
            assemble.push_back("--emit-object");
            assemble.push_back("-c");
            assemble.push_back(source.string());
            assemble.push_back("-o");
            assemble.push_back(object.string());
            const ProcessResult compiled =
                run_process(paths_.krc, assemble);
            append(result.log, compiled.output);
            if (compiled.exit_code != 0) {
                result.error = "krc failed";
                return result;
            }
            const std::filesystem::path crt_object =
                output_directory / (stem + "-crt0.kro");
            std::vector<std::string> crt = common;
            crt.push_back("--emit-object");
            crt.push_back("-c");
            crt.push_back((paths_.sysroot / "kr16" / "lib" / "crt0.s").string());
            crt.push_back("-o");
            crt.push_back(crt_object.string());
            const ProcessResult crt_compiled = run_process(paths_.krc, crt);
            append(result.log, crt_compiled.output);
            if (crt_compiled.exit_code != 0) {
                result.error = "krc failed to assemble the KR16 startup";
                return result;
            }
            std::vector<std::string> link = {
                object.string(),
                crt_object.string(),
                "-o", image.string(),
                "--base=0x100",
                "--entry=_start",
            };
            if (!options.source_root.empty()) {
                link.push_back("--source-root=" + options.source_root);
            }
            const ProcessResult linked =
                run_process(paths_.krld, link);
            append(result.log, linked.output);
            if (linked.exit_code != 0) {
                result.error = "krld failed";
                return result;
            }
        } else {
            std::vector<std::string> compile = common;
            compile.push_back("-c");
            compile.push_back(source.string());
            compile.push_back("-o");
            compile.push_back(image.string());
            const ProcessResult compiled =
                run_process(paths_.krc, compile);
            append(result.log, compiled.output);
            if (compiled.exit_code != 0) {
                result.error = "krc failed";
                return result;
            }
        }
    } else {
        std::vector<std::string> objects;
        if (assembly) {
            objects.push_back(source.string());
        } else {
            std::vector<std::string> compile = common;
            compile.push_back("-c");
            compile.push_back(source.string());
            compile.push_back("-o");
            compile.push_back(object.string());
            const ProcessResult compiled =
                run_process(paths_.krc, compile);
            append(result.log, compiled.output);
            if (compiled.exit_code != 0) {
                result.error = "krc failed";
                return result;
            }
            objects.push_back(object.string());
        }

        std::vector<std::string> link = objects;
        link.insert(link.end(), runtime.begin(), runtime.end());
        link.push_back("-o");
        link.push_back(image.string());
        // A kr232 container records its base itself; a kernel image is based at 0.
        if (options.target != "kr232") {
            link.push_back("--base=" + options.base);
        }
        link.push_back("--entry=" + options.entry);
        if (options.map) {
            link.push_back("--map=" + image.string() + ".map");
        }
        if (!options.source_root.empty()) {
            link.push_back("--source-root=" + options.source_root);
        }
        const ProcessResult linked =
            run_process(paths_.krld, link);
        append(result.log, linked.output);
        if (linked.exit_code != 0) {
            result.error = "krld failed";
            return result;
        }
    }

    if (!std::filesystem::exists(image)) {
        result.error = "the build did not write " + image.string();
        return result;
    }
    result.ok = true;
    result.image = image;
    return result;
}

} // namespace krdbg::host
