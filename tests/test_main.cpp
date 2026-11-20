#include "test_framework.h"

#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    std::string exclude;
    std::string filter;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--exclude" && index + 1 < argc) {
            exclude = argv[++index];
        } else if (argument == "--filter" && index + 1 < argc) {
            filter = argv[++index];
        }
    }
    for (const krdbg::test::Case& test : krdbg::test::registry()) {
        const std::string name = test.name;
        if (!filter.empty() && name.find(filter) == std::string::npos) {
            continue;
        }
        if (!exclude.empty() && name.find(exclude) != std::string::npos) {
            std::cout << "[ SKIP ] " << test.name << "\n";
            continue;
        }
        const int before = krdbg::test::failures();
        test.fn();
        const bool passed = krdbg::test::failures() == before;
        std::cout << (passed ? "[ PASS ] " : "[ FAIL ] ") << test.name << "\n";
    }
    std::cout << krdbg::test::registry().size() << " test(s), "
              << krdbg::test::failures() << " failure(s)\n";
    return krdbg::test::failures() == 0 ? 0 : 1;
}
