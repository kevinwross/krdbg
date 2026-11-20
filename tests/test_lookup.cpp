#include "test_framework.h"

#include <cstdint>

#include "debug_info_util.h"
#include "fake_debug_info.h"

using krdbg::DebugInfo;
using krdbg::FunctionInfo;
using krdbg::LineInfo;
using krdbg::SourceFile;
using krdbg::TypeKind;
using krdbg::TypeInfo;

KRDBG_TEST(fake_debug_info_function_and_line_lookups_are_clamped) {
    const DebugInfo info = krdbg::make_fake_debug_info();

    const krdbg::FunctionInfo* main = krdbg::detail::find_function(info, 0x1004);
    CHECK(main != nullptr);
    CHECK_EQ(main->name, "main");
    CHECK_EQ(krdbg::detail::find_function(info, 0x1018)->name, "util_add");
    CHECK(krdbg::detail::find_function(info, 0x1010) == nullptr);

    CHECK_EQ(krdbg::detail::find_line(info, 0x1000)->line, 4U);
    CHECK_EQ(krdbg::detail::find_line(info, 0x1004)->line, 5U);
    CHECK_EQ(krdbg::detail::find_line(info, 0x1020)->line, 5U);
    CHECK(krdbg::detail::find_line(info, 0x0FFF) == nullptr);

    // A line before the function must not be reported for an address inside it.
    DebugInfo custom;
    custom.functions = {FunctionInfo{"f", 0x2000, 0x2010, 0, 1}};
    custom.lines = {LineInfo{0x1FF0, 0, 9, 0}, LineInfo{0x2004, 0, 10, 0}};
    CHECK(krdbg::detail::find_line(custom, 0x2000) == nullptr);
    CHECK_EQ(krdbg::detail::find_line(custom, 0x2008)->line, 10U);
}

KRDBG_TEST(debug_info_source_lookup_matches_path_name_and_suffix) {
    const DebugInfo info = krdbg::make_fake_debug_info();
    CHECK_EQ(krdbg::detail::find_source(info, "examples/fake/main.c")->name, "main.c");
    CHECK_EQ(krdbg::detail::find_source(info, "util.c")->name, "util.c");
    CHECK_EQ(krdbg::detail::find_source(info, "fake\\main.c")->name, "main.c");
    CHECK(krdbg::detail::find_source(info, "missing.c") == nullptr);
    CHECK(krdbg::detail::find_source(info, "") == nullptr);
}

KRDBG_TEST(debug_info_type_lookup_is_by_id_and_name) {
    const DebugInfo info = krdbg::make_fake_debug_info();
    const TypeInfo* point = krdbg::detail::find_type(info, 1);
    CHECK(point != nullptr);
    CHECK_EQ(point->name, "Point");
    CHECK_EQ(point->kind, TypeKind::Struct);

    const TypeInfo* array = krdbg::detail::find_type(info, "int[4]");
    CHECK(array != nullptr);
    CHECK_EQ(array->kind, TypeKind::Array);
    CHECK(krdbg::detail::find_type(info, krdbg::kInvalidType) == nullptr);
    CHECK(krdbg::detail::find_type(info, "missing") == nullptr);
}
