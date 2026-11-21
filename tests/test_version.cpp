#include "test_framework.h"

#include <string>

#include "krdbg/version.h"

KRDBG_TEST(version_string_matches_the_cmake_project_version) {
    CHECK_EQ(std::string(krdbg::kVersionString),
             std::string(KRDBG_PROJECT_VERSION));
}
