#include "test_framework.h"

#include <cstdint>

#include "krdbg/debug_info.h"

using krdbg::DebugInfo;
using krdbg::FunctionInfo;
using krdbg::LineInfo;
using krdbg::MemberInfo;
using krdbg::SourceFile;
using krdbg::TypeInfo;
using krdbg::VariableInfo;

namespace {

DebugInfo first_payload() {
    DebugInfo info;
    info.target_id = 1;
    info.build_id = 0x111;
    info.compiler = "first";
    info.sources = {SourceFile{1, "a.c", "a.c"}};
    info.functions = {FunctionInfo{"fa", 0x1000, 0x1010, 1, 1}};
    info.lines = {LineInfo{0x1004, 1, 2, 0}};

    TypeInfo int_type;
    int_type.name = "int";
    int_type.size = 4;
    info.types = {int_type};
    info.type_by_name = {{"int", 0}};
    info.symbols = {krdbg::SymbolInfo{"sa", 0x1000, 4, 0}};

    VariableInfo local;
    local.function = "fa";
    local.name = "x";
    local.type = 0;
    local.size = 4;
    local.location = krdbg::Location{krdbg::LocationKind::FrameOffset, -4, {}, 0, 0};
    info.variables = {local};
    return info;
}

DebugInfo second_payload() {
    DebugInfo info;
    info.target_id = 2;
    info.build_id = 0x222;
    info.compiler = "second";
    info.sources = {SourceFile{5, "b.c", "b.c"}};
    info.functions = {
        FunctionInfo{"fb", 0x0800, 0x0810, 5, 1},
        FunctionInfo{"fc", 0x3000, 0x3010, 5, 2},
    };
    info.lines = {
        LineInfo{0x0804, 5, 3, 0},
        LineInfo{0x3004, 5, 4, 1},
    };

    TypeInfo point;
    point.name = "Point";
    point.size = 8;
    point.kind = krdbg::TypeKind::Struct;
    point.members = {MemberInfo{"x", 1, 0}, MemberInfo{"y", 1, 4}};
    TypeInfo int_type;
    int_type.name = "int";
    int_type.size = 4;
    info.types = {point, int_type};
    info.type_by_name = {{"Point", 0}, {"int", 1}};
    info.symbols = {krdbg::SymbolInfo{"sb", 0x0800, 4, 0}};

    VariableInfo aggregate;
    aggregate.function = "fb";
    aggregate.name = "p";
    aggregate.type = 0;
    aggregate.size = 8;
    aggregate.location = krdbg::Location{krdbg::LocationKind::FrameOffset, -8, {}, 0, 0};
    info.variables = {aggregate};
    return info;
}

} // namespace

KRDBG_TEST(merge_debug_info_renumbers_sources_and_types) {
    DebugInfo into = first_payload();
    const DebugInfo other = second_payload();
    krdbg::merge_debug_info(into, other);

    CHECK_EQ(into.sources.size(), 2U);
    CHECK_EQ(into.sources[0].id, 0U);
    CHECK_EQ(into.sources[1].id, 1U);
    CHECK_EQ(into.sources[1].path, "b.c");

    CHECK_EQ(into.types.size(), 3U);
    CHECK_EQ(into.types[1].name, "Point");
    CHECK_EQ(into.types[2].name, "int");
    CHECK_EQ(into.types[1].members[0].type, 2U);
    CHECK_EQ(into.type_by_name.at("Point"), 1U);
    // The primary payload wins a duplicate type name.
    CHECK_EQ(into.type_by_name.at("int"), 0U);

    CHECK_EQ(into.symbols[1].type, 1U);
    CHECK_EQ(into.variables[1].type, 1U);

    // Build metadata belongs to the primary payload.
    CHECK_EQ(into.target_id, 1U);
    CHECK_EQ(into.build_id, 0x111U);
    CHECK_EQ(into.compiler, "first");
}

KRDBG_TEST(merge_debug_info_sorts_functions_and_lines_and_remaps_indexes) {
    DebugInfo into = first_payload();
    const DebugInfo other = second_payload();
    krdbg::merge_debug_info(into, other);

    CHECK_EQ(into.functions.size(), 3U);
    CHECK_EQ(into.functions[0].name, "fb");
    CHECK_EQ(into.functions[1].name, "fa");
    CHECK_EQ(into.functions[2].name, "fc");
    CHECK_EQ(into.functions[0].source, 1U);
    CHECK_EQ(into.functions[1].source, 0U);

    CHECK_EQ(into.lines.size(), 3U);
    CHECK_EQ(into.lines[0].address, 0x0804U);
    CHECK_EQ(into.lines[0].source, 1U);
    CHECK_EQ(into.lines[0].function, 0U);
    CHECK_EQ(into.lines[1].address, 0x1004U);
    CHECK_EQ(into.lines[1].function, 1U);
    CHECK_EQ(into.lines[2].address, 0x3004U);
    CHECK_EQ(into.lines[2].function, 2U);
}

KRDBG_TEST(merge_debug_info_ignores_a_self_merge) {
    DebugInfo into = first_payload();
    krdbg::merge_debug_info(into, into);
    CHECK_EQ(into.sources.size(), 1U);
    CHECK_EQ(into.functions.size(), 1U);
    CHECK_EQ(into.types.size(), 1U);
}
