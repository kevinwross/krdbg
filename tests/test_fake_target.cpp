#include "test_framework.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "fake_target.h"

using krdbg::FakeTarget;
using krdbg::RawFrame;
using krdbg::ScalarValue;

KRDBG_TEST(fake_target_memory_round_trips_and_rejects_out_of_range) {
    FakeTarget target;
    const std::array<std::uint8_t, 4> written{1, 2, 3, 4};
    CHECK(target.write_memory(0x2000, written));

    std::array<std::uint8_t, 4> read{};
    CHECK(target.read_memory(0x2000, read));
    CHECK(read == written);

    std::array<std::uint8_t, 4> outside{};
    CHECK(!target.read_memory(0x0FFF, outside));
    CHECK(!target.read_memory(0x10FFE, outside));
    CHECK(!target.write_memory(0x0FFF, written));
}

KRDBG_TEST(fake_target_unwind_follows_the_call_stack) {
    FakeTarget target;
    std::vector<RawFrame> frames = target.unwind();
    CHECK_EQ(frames.size(), 1U);
    CHECK_EQ(frames[0].pc, 0x1000U);
    CHECK_EQ(frames[0].frame_base, 0x2100U);

    CHECK(target.set_pc(0x1008));
    CHECK_EQ(target.step_instruction().kind, krdbg::StopKind::Retired);
    CHECK_EQ(target.pc(), 0x1018U);
    frames = target.unwind();
    CHECK_EQ(frames.size(), 2U);
    CHECK_EQ(frames[0].pc, 0x1018U);
    CHECK_EQ(frames[0].frame_base, 0x2080U);
    CHECK_EQ(frames[1].pc, 0x100CU);
    CHECK_EQ(frames[1].frame_base, 0x2100U);
}

KRDBG_TEST(fake_target_disassembles_known_and_unknown_code) {
    FakeTarget target;
    krdbg::InstructionView view;
    CHECK(target.disassemble(0x1008, view));
    CHECK_EQ(view.mnemonic, "CALL");
    CHECK_EQ(view.operands, "0x1018");
    CHECK_EQ(view.length, 4U);

    CHECK(target.disassemble(0x1002, view));
    CHECK_EQ(view.mnemonic, "NOP");

    CHECK(!target.disassemble(0x2000, view));
}

KRDBG_TEST(fake_target_com_port_handshake_in_both_directions) {
    FakeTarget target;
    krdbg::IComPort* port = target.com_port();
    CHECK(port != nullptr);

    std::string output;
    std::uint8_t byte = 0;
    while (port->read_output(byte)) {
        output.push_back(static_cast<char>(byte));
    }
    CHECK_EQ(output, std::string("fake ready\n"));
    CHECK(port->status().tx_ready);

    CHECK(port->write_input(0x41));
    CHECK(port->status().rx_ready);
    CHECK(!port->write_input(0x42));
    CHECK(port->status().overrun);

    port->clear_overrun();
    CHECK(!port->status().overrun);
    CHECK(target.com().take_input());
    CHECK(!port->status().rx_ready);
}

KRDBG_TEST(fake_target_steps_and_counts_instructions) {
    FakeTarget target;
    CHECK_EQ(target.pc(), 0x1000U);
    CHECK_EQ(target.instructions_executed(), 0U);

    CHECK_EQ(target.step_instruction().kind, krdbg::StopKind::Retired);
    CHECK_EQ(target.pc(), 0x1004U);
    CHECK_EQ(target.instructions_executed(), 1U);

    CHECK_EQ(target.run(3).kind, krdbg::StopKind::Retired);
    CHECK_EQ(target.pc(), 0x101CU);
    CHECK_EQ(target.instructions_executed(), 4U);

    target.set_halted(true);
    CHECK_EQ(target.step_instruction().kind, krdbg::StopKind::Halted);
    CHECK_EQ(target.instructions_executed(), 4U);
}

KRDBG_TEST(fake_target_contexts_keep_independent_state) {
    FakeTarget target;
    target.add_context("Core 1");
    const std::vector<krdbg::ContextInfo> contexts = target.contexts();
    CHECK_EQ(contexts.size(), 2U);
    CHECK_EQ(contexts[0].id, 1U);
    CHECK_EQ(contexts[1].id, 2U);

    CHECK(target.select_context(2));
    CHECK(target.set_pc(0x1008));
    CHECK_EQ(target.pc(), 0x1008U);
    CHECK(target.select_context(1));
    CHECK_EQ(target.pc(), 0x1000U);
    CHECK(!target.select_context(99));
}

KRDBG_TEST(fake_target_register_groups_read_and_write) {
    FakeTarget target;
    ScalarValue value;
    CHECK(target.read_value("gpr", 0, value));
    CHECK_EQ(value.bits, 0x2AU);

    CHECK(target.write_value("gpr", 0, ScalarValue{0x1234, 32}));
    CHECK(target.read_value("gpr", 0, value));
    CHECK_EQ(value.bits, 0x1234U);

    CHECK(target.write_flags(0x5));
    std::uint64_t flags = 0;
    CHECK(target.read_flags(flags));
    CHECK_EQ(flags, 0x5U);
    CHECK(target.read_value("flags", 0, value));
    CHECK_EQ(value.bits, 1U);
}

KRDBG_TEST(fake_target_video_has_4_3_and_16_9_modes) {
    FakeTarget target;
    krdbg::FramebufferView frame;
    CHECK(target.video()->frame(frame));
    CHECK_EQ(frame.width, 320);
    CHECK_EQ(frame.height, 240);
    CHECK_EQ(frame.stride, 1280);
    CHECK(!frame.pixels.empty());

    target.video_source().set_mode(384, 216);
    CHECK(target.video()->frame(frame));
    CHECK_EQ(frame.width, 384);
    CHECK_EQ(frame.height, 216);
    CHECK_EQ(frame.frame_number, 2U);
}

// The text terminal is an optional third device. It is off by default so every
// golden and scene written before it existed is unchanged, and a target that
// enables it exposes a whole cell grid and its own input channel.
KRDBG_TEST(fake_target_text_display_is_optional_and_needs_opting_in) {
    FakeTarget plain;
    CHECK(plain.text_display() == nullptr);
    CHECK(!plain.layout(1).has_text_display);

    FakeTarget with_display(true, true, true);
    CHECK(with_display.text_display() != nullptr);
    CHECK(with_display.layout(1).has_text_display);
    CHECK_EQ(with_display.text_display()->name(), std::string("DISPLAY"));

    // A display-only target needs neither the COM port nor a framebuffer.
    FakeTarget display_only(false, false, true);
    CHECK(display_only.text_display() != nullptr);
    CHECK(display_only.com_port() == nullptr);
    CHECK(display_only.video() == nullptr);
    CHECK(display_only.layout(1).has_text_display);
}

KRDBG_TEST(fake_text_display_frame_is_deterministic) {
    FakeTarget target(true, true, true);
    krdbg::ITextDisplay* display = target.text_display();
    CHECK(display != nullptr);
    if (display == nullptr) {
        return;
    }
    krdbg::TextDisplayView first;
    CHECK(display->frame(first));
    CHECK_EQ(first.rows, krdbg::FakeTextDisplay::kRows);
    CHECK_EQ(first.cols, krdbg::FakeTextDisplay::kCols);
    CHECK_EQ(first.cells.size(),
             static_cast<std::size_t>(first.rows) * static_cast<std::size_t>(first.cols));
    CHECK(first.cursor.visible);
    const std::vector<krdbg::TextCell> snapshot(first.cells.begin(), first.cells.end());

    krdbg::TextDisplayView second;
    CHECK(display->frame(second));
    CHECK_EQ(second.cells.size(), snapshot.size());
    for (std::size_t index = 0; index < snapshot.size(); ++index) {
        CHECK(second.cells[index] == snapshot[index]);
    }

    target.display_source().set_char(4, 4, 'Z');
    krdbg::TextDisplayView third;
    CHECK(display->frame(third));
    CHECK(third.cells[4U * static_cast<std::size_t>(third.cols) + 4U].code == 'Z');
}

KRDBG_TEST(fake_text_display_records_forwarded_input) {
    FakeTarget target(true, true, true);
    krdbg::ITextDisplay* display = target.text_display();
    CHECK(display != nullptr);
    if (display == nullptr) {
        return;
    }
    CHECK(display->send_key(8));
    CHECK(display->send_key('R'));
    CHECK(display->send_text("hello\n"));
    CHECK(display->send_click(2, 3, 1));

    const std::vector<int>& keys = target.display_source().keys();
    CHECK_EQ(keys.size(), 2U);
    CHECK_EQ(keys[0], 8);
    CHECK_EQ(keys[1], 'R');
    CHECK_EQ(target.display_source().text(), std::string("hello\n"));
    const std::vector<krdbg::FakeTextDisplay::Click>& clicks =
        target.display_source().clicks();
    CHECK_EQ(clicks.size(), 1U);
    CHECK_EQ(clicks[0].row, 2);
    CHECK_EQ(clicks[0].col, 3);
    CHECK_EQ(clicks[0].buttons, 1);

    target.display_source().clear_input();
    CHECK(target.display_source().keys().empty());
    CHECK(target.display_source().text().empty());
}
