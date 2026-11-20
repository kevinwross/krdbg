#pragma once

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace krdbg::test {

struct Case {
    const char* name = "";
    void (*fn)() = nullptr;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

inline void fail(const char* file, int line, const std::string& message) {
    ++failures();
    std::cerr << file << ":" << line << ": FAIL: " << message << "\n";
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back(Case{name, fn}); }
};

} // namespace krdbg::test

#define KRDBG_TEST(name)                                                              \
    static void name();                                                               \
    static const ::krdbg::test::Registrar name##_registrar(#name, &name);             \
    static void name()

#define CHECK(expression)                                                             \
    do {                                                                              \
        if (!(expression)) {                                                          \
            ::krdbg::test::fail(__FILE__, __LINE__, #expression);                     \
        }                                                                             \
    } while (false)

#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                              \
        if (!((actual) == (expected))) {                                              \
            ::krdbg::test::fail(__FILE__, __LINE__,                                   \
                                #actual " == " #expected);                            \
        }                                                                             \
    } while (false)
