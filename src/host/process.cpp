#include "process.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace krdbg::host {
namespace {

#if defined(_WIN32)
std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

std::wstring quote(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result = L"\"";
    for (const wchar_t ch : argument) {
        if (ch == L'"') {
            result += L"\\\"";
        } else {
            result += ch;
        }
    }
    result += L"\"";
    return result;
}
#endif

} // namespace

ProcessResult run_process(const std::filesystem::path& executable,
                          const std::vector<std::string>& arguments,
                          const std::filesystem::path& working_directory) {
    ProcessResult result;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) {
        result.output = "CreatePipe failed";
        return result;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring command_line = quote(executable.wstring());
    for (const std::string& argument : arguments) {
        command_line += L" ";
        command_line += quote(widen(argument));
    }
    const std::wstring directory = working_directory.empty()
                                       ? std::wstring{}
                                       : working_directory.wstring();
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        executable.wstring().c_str(), command_line.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr,
        directory.empty() ? nullptr : directory.c_str(), &startup, &process);
    CloseHandle(write_pipe);
    if (!started) {
        CloseHandle(read_pipe);
        result.output = "CreateProcess failed: " +
                        std::to_string(static_cast<unsigned long>(GetLastError()));
        return result;
    }

    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &read, nullptr) &&
           read > 0) {
        result.output.append(buffer.data(), read);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_pipe);
    result.exit_code = static_cast<int>(exit_code);
    return result;
#else
    int pipes[2] = {-1, -1};
    if (pipe(pipes) != 0) {
        result.output = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }
    const pid_t child = fork();
    if (child < 0) {
        close(pipes[0]);
        close(pipes[1]);
        result.output = std::string("fork failed: ") + std::strerror(errno);
        return result;
    }
    if (child == 0) {
        close(pipes[0]);
        dup2(pipes[1], STDOUT_FILENO);
        dup2(pipes[1], STDERR_FILENO);
        close(pipes[1]);
        if (!working_directory.empty()) {
            (void)chdir(working_directory.c_str());
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const std::string& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(pipes[1]);
    std::array<char, 4096> buffer{};
    ssize_t read = 0;
    while ((read = ::read(pipes[0], buffer.data(), buffer.size())) > 0) {
        result.output.append(buffer.data(), static_cast<std::size_t>(read));
    }
    close(pipes[0]);
    int status = 0;
    waitpid(child, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return result;
#endif
}

} // namespace krdbg::host
