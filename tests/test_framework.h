#pragma once

#include <cstdlib>
#include <fstream>
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

// KRDBG_UPDATE_GOLDENS=1 rewrites recorded hashes for an intentional visual
// change. The gate never sets it.
inline bool update_goldens() {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "KRDBG_UPDATE_GOLDENS") != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = value[0] != '\0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv("KRDBG_UPDATE_GOLDENS");
    return value != nullptr && value[0] != '\0';
#endif
}

inline void write_golden(const std::string& path, const std::string& value) {
    std::ofstream output(path);
    output << value << "\n";
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
