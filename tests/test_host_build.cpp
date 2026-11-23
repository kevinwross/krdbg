#include "test_framework.h"

#include "build_chain.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "krdbg/session.h"

#if defined(KRDBG_HAVE_KR16)
#include "kr16_adapter.h"
#endif
#if defined(KRDBG_HAVE_KR32)
#include "kr32_adapter.h"
#endif

namespace {

std::string env_value(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result;
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::filesystem::path source_root() {
    return std::filesystem::path(KRDBG_SOURCE_ROOT);
}

krdbg::host::BuildOptions build_options(const std::string& target,
                                        const std::string& root) {
    krdbg::host::BuildOptions options;
    options.target = target;
    options.source_root = root;
    return options;
}

[[maybe_unused]] std::unique_ptr<krdbg::DebugSession> load_kr16(
    const std::filesystem::path& image, const std::string& root,
    std::string& error) {
#if defined(KRDBG_HAVE_KR16)
    krdbg::kr16adapter::LoadedProgram loaded;
    krdbg::kr16adapter::Options options;
    options.source_roots = {root};
    if (!krdbg::kr16adapter::load_image(image.string(), options, loaded,
                                        error)) {
        return nullptr;
    }
    return std::make_unique<krdbg::DebugSession>(
        std::move(loaded.target), loaded.info,
        std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
#else
    (void)image;
    (void)root;
    error = "no KR16 adapter";
    return nullptr;
#endif
}

} // namespace

KRDBG_TEST(host_build_chain_builds_c_cpp_and_s) {
    const std::string prefix = env_value("KRDBG_KRC_PREFIX");
    if (prefix.empty()) {
        return;
    }
    krdbg::host::ToolchainPaths paths;
    std::string error;
    CHECK(krdbg::host::discover_toolchain(prefix, paths, error));
    krdbg::host::BuildChain chain(paths);
    const std::string root = source_root().string();

#if defined(KRDBG_HAVE_KR16)
    const krdbg::host::BuildResult source = chain.build(
        source_root() / "tests" / "fixtures" / "kr16" / "m4.c",
        build_options("kr16", root));
    CHECK(source.ok);
    if (!source.ok) {
        CHECK_EQ(source.error, std::string{});
        return;
    }
    std::unique_ptr<krdbg::DebugSession> session =
        load_kr16(source.image, root, error);
    CHECK(session != nullptr);
    if (session != nullptr) {
        const krdbg::LineInfo* line =
            session->line_at(session->pc());
        CHECK(line == nullptr || line->line != 0U);
        CHECK(!session->info().functions.empty());
        CHECK(!session->info().sources.empty());
        if (!session->info().sources.empty()) {
            const std::string source_path = session->info().sources[0].path;
            CHECK(session->run_to_line(source_path, 18).kind ==
                  krdbg::StopKind::Retired);
            const std::vector<krdbg::VariableView> locals =
                session->locals(0);
            bool saw_point = false;
            for (const krdbg::VariableView& variable : locals) {
                saw_point = saw_point || variable.name == "point";
            }
            CHECK(saw_point);
            CHECK(session->step(krdbg::StepMode::LineOver).kind ==
                  krdbg::StopKind::Retired);
        }
    }


#if defined(KRDBG_HAVE_KR232)
    // kr232 builds through the same chain: the prefix must be installed at the
    // revision the log records, or this returns without proving anything.
    const krdbg::host::BuildResult built = chain.build(
        source_root() / "tests" / "fixtures" / "kr232" / "host_build.c",
        build_options("kr232", root));
    CHECK(built.ok);
    if (built.ok) {
        krdbg::kr232adapter::LoadedProgram loaded;
        krdbg::kr232adapter::Options adapter_options;
        adapter_options.source_roots = {root};
        CHECK(krdbg::kr232adapter::load_image(built.image.string(),
                                             adapter_options, loaded, error));
        CHECK(loaded.info.has_value());
        if (loaded.info.has_value() && !loaded.info->lines.empty()) {
            const std::string source_path = loaded.info->sources.front().path;
            auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
            krdbg::DebugSession session(std::move(loaded.target), loaded.info, reload);
            const krdbg::StopResult stopped =
                session.run_to_line(source_path, 3);
            CHECK(stopped.kind == krdbg::StopKind::Retired ||
                  stopped.kind == krdbg::StopKind::Halted);
            CHECK(!session.info().functions.empty());
        }
    }
#endif

    const std::filesystem::path cpp_source =
        std::filesystem::path("build") / "m5-host" / "hello.cpp";
    std::filesystem::create_directories(cpp_source.parent_path());
    std::ofstream cpp(cpp_source);
    cpp << "int main() { int value = 3; return value + 4; }\n";
    cpp.close();
    const krdbg::host::BuildResult cpp_built =
        chain.build(cpp_source, build_options("kr16", root));
    CHECK(cpp_built.ok);
    if (cpp_built.ok) {
        std::unique_ptr<krdbg::DebugSession> cpp_session =
            load_kr16(cpp_built.image, root, error);
        CHECK(cpp_session != nullptr);
    }

    const std::filesystem::path assembly =
        std::filesystem::path("build") / "m5-host" / "main.s";
    std::ofstream asm_file(assembly);
    asm_file << ".text\n.global main\nmain:\n MOV R0, #7\n RET\n";
    asm_file.close();
    const krdbg::host::BuildResult assembled =
        chain.build(assembly, build_options("kr16", root));
    CHECK(assembled.ok);
    if (assembled.ok) {
        std::unique_ptr<krdbg::DebugSession> asm_session =
            load_kr16(assembled.image, root, error);
        CHECK(asm_session != nullptr);
    }
#endif

#if defined(KRDBG_HAVE_KR32)
    const krdbg::host::BuildResult kr32 = chain.build(
        source_root() / "tests" / "fixtures" / "kr32" / "m3.c",
        build_options("kr32", root));
    CHECK(kr32.ok);
#endif
}

KRDBG_TEST(host_rebuild_preserves_line_breakpoints) {
    const std::string prefix = env_value("KRDBG_KRC_PREFIX");
    if (prefix.empty()) {
        return;
    }
#if defined(KRDBG_HAVE_KR16)
    krdbg::host::ToolchainPaths paths;
    std::string error;
    CHECK(krdbg::host::discover_toolchain(prefix, paths, error));
    krdbg::host::BuildChain chain(paths);
    const std::string root = source_root().string();
    const std::filesystem::path source =
        source_root() / "tests" / "fixtures" / "kr16" / "m4.c";
    const krdbg::host::BuildResult first =
        chain.build(source, build_options("kr16", root));
    CHECK(first.ok);
    if (!first.ok) {
        return;
    }
    std::unique_ptr<krdbg::DebugSession> session =
        load_kr16(first.image, root, error);
    CHECK(session != nullptr);
    if (session == nullptr) {
        return;
    }
    session->add_line_breakpoint(
        session->info().sources.empty() ? std::string{} : session->info().sources[0].path,
        18);
    CHECK_EQ(session->breakpoints().size(), 1U);
    const krdbg::host::BuildResult second =
        chain.build(source, build_options("kr16", root));
    CHECK(second.ok);
    if (second.ok) {
        krdbg::kr16adapter::LoadedProgram loaded;
        krdbg::kr16adapter::Options options;
        options.source_roots = {root};
        CHECK(krdbg::kr16adapter::load_image(second.image.string(), options,
                                             loaded, error));
        session->replace_target(std::move(loaded.target), loaded.info);
        CHECK(session->breakpoints().size() == 1U);
    }
#endif
}
