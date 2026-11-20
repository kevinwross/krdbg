#include "test_framework.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "fake_debug_info.h"
#include "fake_target.h"
#include "variables.h"

using krdbg::DebugInfo;
using krdbg::LocationKind;
using krdbg::MemberView;
using krdbg::ScalarValue;
using krdbg::TypeInfo;
using krdbg::VariableInfo;

namespace {

VariableInfo make_variable(std::uint32_t size, krdbg::TypeId type,
                           krdbg::Location location) {
    VariableInfo variable;
    variable.name = "v";
    variable.size = size;
    variable.type = type;
    variable.location = location;
    return variable;
}

} // namespace

KRDBG_TEST(variable_storage_reads_a_frame_offset_from_memory) {
    krdbg::FakeTarget target;
    const std::array<std::uint8_t, 4> bytes{42, 0, 0, 0};
    CHECK(target.write_memory(0x20FC, bytes));

    const VariableInfo variable = make_variable(
        4, 0, krdbg::Location{LocationKind::FrameOffset, -4, {}, 0, 0});
    std::vector<std::uint8_t> storage;
    CHECK(krdbg::detail::read_variable_storage(target, 0x2100, variable, storage));
    CHECK_EQ(storage.size(), 4U);
    CHECK_EQ(storage[0], 42U);

    const TypeInfo int_type = [] {
        TypeInfo type;
        type.name = "int";
        type.size = 4;
        type.kind = krdbg::TypeKind::Builtin;
        return type;
    }();
    CHECK_EQ(krdbg::detail::format_scalar(&int_type, storage, true),
             std::string("42"));
}

KRDBG_TEST(variable_storage_reads_absolute_and_register_locations) {
    krdbg::FakeTarget target;
    const std::array<std::uint8_t, 4> bytes{7, 0, 0, 0};
    CHECK(target.write_memory(0x2004, bytes));
    CHECK(target.write_value("gpr", 0, ScalarValue{0x12345678U, 32}));

    std::vector<std::uint8_t> storage;
    const VariableInfo absolute = make_variable(
        4, 0, krdbg::Location{LocationKind::Absolute, 0, {}, 0, 0x2004});
    CHECK(krdbg::detail::read_variable_storage(target, 0x2100, absolute, storage));
    CHECK_EQ(storage[0], 7U);

    const VariableInfo reg =
        make_variable(4, 0, krdbg::Location{LocationKind::Register, 0, "gpr", 0, 0});
    CHECK(krdbg::detail::read_variable_storage(target, 0x2100, reg, storage));
    CHECK_EQ(storage.size(), 4U);
    CHECK_EQ(storage[0], 0x78U);
    CHECK_EQ(storage[3], 0x12U);
}

KRDBG_TEST(variable_storage_rejects_unreadable_locations) {
    krdbg::FakeTarget target;
    std::vector<std::uint8_t> storage;
    const VariableInfo none = make_variable(
        4, 0, krdbg::Location{LocationKind::None, 0, {}, 0, 0});
    CHECK(!krdbg::detail::read_variable_storage(target, 0x2100, none, storage));

    const VariableInfo outside = make_variable(
        4, 0, krdbg::Location{LocationKind::Absolute, 0, {}, 0, 0});
    CHECK(!krdbg::detail::read_variable_storage(target, 0x2100, outside, storage));
}

KRDBG_TEST(register_held_aggregate_expands_through_its_type) {
    krdbg::FakeTarget target;
    CHECK(target.write_value("core", 5, ScalarValue{0x0000000200000001ULL, 64}));

    const VariableInfo point = make_variable(
        8, 1, krdbg::Location{LocationKind::Register, 0, "core", 5, 0});
    std::vector<std::uint8_t> storage;
    CHECK(krdbg::detail::read_variable_storage(target, 0x2100, point, storage));
    CHECK_EQ(storage.size(), 8U);

    const DebugInfo info = krdbg::make_fake_debug_info();
    const std::vector<MemberView> members =
        krdbg::detail::expand_members(info, 1, storage, true);
    CHECK_EQ(members.size(), 2U);
    CHECK_EQ(members[0].name, "x");
    CHECK_EQ(members[0].value, "1");
    CHECK(!members[0].has_children);
    CHECK_EQ(members[1].name, "y");
    CHECK_EQ(members[1].value, "2");
}

KRDBG_TEST(aggregate_expansion_reports_missing_storage_and_pointers) {
    const DebugInfo info = krdbg::make_fake_debug_info();
    const std::array<std::uint8_t, 4> short_storage{1, 0, 0, 0};
    const std::vector<MemberView> partial =
        krdbg::detail::expand_members(info, 1, short_storage, true);
    CHECK_EQ(partial.size(), 2U);
    CHECK_EQ(partial[0].value, "1");
    CHECK_EQ(partial[1].value, "?");

    TypeInfo pointer;
    pointer.name = "Point*";
    pointer.size = 4;
    pointer.kind = krdbg::TypeKind::Pointer;
    const std::array<std::uint8_t, 4> address{0x34, 0x12, 0x00, 0x00};
    CHECK_EQ(krdbg::detail::format_scalar(&pointer, address, true),
             std::string("0x00001234"));
}
