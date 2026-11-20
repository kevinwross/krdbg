#include "test_framework.h"

#include "fake_debug_info.h"

using krdbg::DebugInfo;
using krdbg::LocationKind;
using krdbg::TypeKind;
using krdbg::VariableGlobal;
using krdbg::VariableInfo;

KRDBG_TEST(fake_debug_info_covers_sources_lines_and_functions) {
    const DebugInfo info = krdbg::make_fake_debug_info();
    CHECK_EQ(info.sources.size(), 2U);
    CHECK_EQ(info.functions.size(), 2U);
    CHECK_EQ(info.lines.size(), 7U);
    CHECK_EQ(info.symbols.size(), 3U);

    CHECK_EQ(info.sources[0].name, "main.c");
    CHECK_EQ(info.functions[0].name, "main");
    CHECK_EQ(info.functions[0].begin, 0x1000U);
    CHECK_EQ(info.functions[0].end, 0x1010U);
    CHECK_EQ(info.lines.front().address, 0x1000U);
    CHECK_EQ(info.lines.back().address, 0x1020U);

    for (std::size_t index = 1; index < info.lines.size(); ++index) {
        CHECK(info.lines[index - 1].address < info.lines[index].address);
    }
}

KRDBG_TEST(fake_debug_info_has_an_aggregate_type_and_members) {
    const DebugInfo info = krdbg::make_fake_debug_info();
    const auto point = info.type_by_name.find("Point");
    CHECK(point != info.type_by_name.end());
    CHECK(point != info.type_by_name.end() && point->second == 1U);
    CHECK_EQ(info.types[1].kind, TypeKind::Struct);
    CHECK_EQ(info.types[1].size, 8U);
    CHECK_EQ(info.types[1].members.size(), 2U);
    CHECK_EQ(info.types[1].members[0].name, "x");
    CHECK_EQ(info.types[1].members[1].offset, 4U);

    const auto array = info.type_by_name.find("int[4]");
    CHECK(array != info.type_by_name.end());
    CHECK_EQ(info.types[3].kind, TypeKind::Array);
    CHECK_EQ(info.types[3].element_count, 4U);
}

KRDBG_TEST(fake_debug_info_has_register_frame_and_absolute_locations) {
    const DebugInfo info = krdbg::make_fake_debug_info();
    CHECK_EQ(info.variables.size(), 9U);

    bool has_register = false;
    bool has_frame = false;
    bool has_absolute = false;
    bool has_global = false;
    for (const VariableInfo& variable : info.variables) {
        has_register = has_register || variable.location.kind == LocationKind::Register;
        has_frame = has_frame || variable.location.kind == LocationKind::FrameOffset;
        has_absolute = has_absolute || variable.location.kind == LocationKind::Absolute;
        has_global = has_global || (variable.flags & VariableGlobal) != 0U;
    }
    CHECK(has_register);
    CHECK(has_frame);
    CHECK(has_absolute);
    CHECK(has_global);
}
