#include "fake_debug_info.h"

#include <string>

namespace krdbg {

DebugInfo make_fake_debug_info() {
    DebugInfo info;
    info.target_id = 1;
    info.abi_version = 1;
    info.pointer_bits = 32;
    info.build_id = 0x00F00D42ULL;
    info.compiler = "krdbg fake builder";

    info.sources = {
        SourceFile{1, "examples/fake/main.c", "main.c"},
        SourceFile{2, "examples/fake/util.c", "util.c"},
    };

    // Type ids are vector indexes. id 0 is int, so kInvalidType stays distinct.
    TypeInfo int_type;
    int_type.name = "int";
    int_type.size = 4;
    int_type.kind = TypeKind::Builtin;
    info.types.push_back(int_type);

    TypeInfo point_type;
    point_type.name = "Point";
    point_type.size = 8;
    point_type.kind = TypeKind::Struct;
    point_type.members = {
        MemberInfo{"x", 0, 0},
        MemberInfo{"y", 0, 4},
    };
    info.types.push_back(point_type);
    const TypeId point_id = 1;

    TypeInfo pointer_type;
    pointer_type.name = "Point*";
    pointer_type.size = 4;
    pointer_type.kind = TypeKind::Pointer;
    pointer_type.element = point_id;
    info.types.push_back(pointer_type);

    TypeInfo array_type;
    array_type.name = "int[4]";
    array_type.size = 16;
    array_type.kind = TypeKind::Array;
    array_type.element = 0;
    array_type.element_count = 4;
    info.types.push_back(array_type);

    info.type_by_name = {
        {"int", 0},
        {"Point", point_id},
        {"Point*", 2},
        {"int[4]", 3},
    };

    info.functions = {
        FunctionInfo{"main", 0x1000, 0x1010, 1, 4},
        FunctionInfo{"util_add", 0x1018, 0x1024, 2, 3},
    };

    info.lines = {
        LineInfo{0x1000, 1, 4, 0},
        LineInfo{0x1004, 1, 5, 0},
        LineInfo{0x1008, 1, 6, 0},
        LineInfo{0x100C, 1, 7, 0},
        LineInfo{0x1018, 2, 3, 1},
        LineInfo{0x101C, 2, 4, 1},
        LineInfo{0x1020, 2, 5, 1},
    };

    info.symbols = {
        SymbolInfo{"main", 0x1000, 0x10, 0},
        SymbolInfo{"util_add", 0x1018, 0x0C, 0},
        SymbolInfo{"global_counter", 0x2000, 4, 0},
    };

    VariableInfo argc;
    argc.function = "main";
    argc.name = "argc";
    argc.type = 0;
    argc.size = 4;
    argc.location = Location{LocationKind::Register, 0, "gpr", 0, 0};
    argc.decl_line = 4;
    argc.flags = VariableParam;
    info.variables.push_back(argc);

    VariableInfo total;
    total.function = "main";
    total.name = "total";
    total.type = 0;
    total.size = 4;
    total.location = Location{LocationKind::FrameOffset, -4, {}, 0, 0};
    total.decl_line = 5;
    total.flags = VariableLocal;
    info.variables.push_back(total);

    VariableInfo origin;
    origin.function = "main";
    origin.name = "origin";
    origin.type = point_id;
    origin.size = 8;
    origin.location = Location{LocationKind::FrameOffset, -16, {}, 0, 0};
    origin.decl_line = 6;
    origin.flags = VariableLocal;
    info.variables.push_back(origin);

    VariableInfo values;
    values.function = "main";
    values.name = "values";
    values.type = 3;
    values.size = 16;
    values.location = Location{LocationKind::FrameOffset, -32, {}, 0, 0};
    values.decl_line = 7;
    values.flags = VariableLocal;
    info.variables.push_back(values);

    VariableInfo counter;
    counter.function = "";
    counter.name = "global_counter";
    counter.type = 0;
    counter.size = 4;
    counter.location = Location{LocationKind::Absolute, 0, {}, 0, 0x2000};
    counter.flags = VariableGlobal;
    info.variables.push_back(counter);

    VariableInfo a;
    a.function = "util_add";
    a.name = "a";
    a.type = 0;
    a.size = 4;
    a.location = Location{LocationKind::Register, 0, "gpr", 0, 0};
    a.decl_line = 3;
    a.flags = VariableParam;
    info.variables.push_back(a);

    VariableInfo b;
    b.function = "util_add";
    b.name = "b";
    b.type = 0;
    b.size = 4;
    b.location = Location{LocationKind::Register, 0, "gpr", 1, 0};
    b.decl_line = 3;
    b.flags = VariableParam;
    info.variables.push_back(b);

    return info;
}

} // namespace krdbg
