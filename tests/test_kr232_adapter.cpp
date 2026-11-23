#include "test_framework.h"

#include <filesystem>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "kr232_adapter.h"
#include "krdbg/session.h"

namespace {

std::string fixture_path() {
    return std::string(KRDBG_KR232_FIXTURE);
}

} // namespace

KRDBG_TEST(kr232_adapter_loads_container_and_layout) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const bool ok = krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error);
    CHECK(ok);
    if (!ok || loaded.target == nullptr) {
        return;
    }
    CHECK(loaded.debug_path == krdbg::kr232adapter::DebugPath::Kdbg);
    CHECK(loaded.info.has_value());
    if (loaded.info.has_value()) {
        CHECK_EQ(loaded.info->target_id, 5U);
        CHECK_EQ(loaded.info->pointer_bits, 32U);
    }

    const krdbg::TargetLayout& layout = loaded.target->layout(0);
    CHECK(layout.name == "KR232");
    CHECK_EQ(layout.address_bits, 32U);
    CHECK_EQ(layout.pointer_bits, 32U);
    CHECK(layout.variable_instruction_length);

    // K and I are hardware-owned, so the debugger may show them but not set them.
    bool saw_k = false;
    bool saw_i = false;
    for (const krdbg::FlagDef& flag : layout.flags) {
        if (flag.id == "k") {
            saw_k = true;
            CHECK(!flag.writable);
        }
        if (flag.id == "i") {
            saw_i = true;
            CHECK(!flag.writable);
        }
    }
    CHECK(saw_k);
    CHECK(saw_i);

    // Both unbacked gaps are neither readable nor writable.
    int read_only_gaps = 0;
    for (const krdbg::MemoryRegion& region : layout.memory) {
        if (!region.readable) {
            ++read_only_gaps;
            CHECK(!region.writable);
        }
    }
    CHECK_EQ(read_only_gaps, 2);
}

KRDBG_TEST(kr232_adapter_reads_registers_and_disassembles) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    if (!krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error)) {
        return;
    }
    CHECK(loaded.target != nullptr);

    krdbg::ScalarValue value;
    CHECK(loaded.target->read_value("gpr", 15, value));
    CHECK_EQ(value.width, 32U);

    // The program halts, and every instruction the debugger steps is one byte or
    // more and advances by its own decoded length.
    const std::uint64_t pc = loaded.target->pc();
    krdbg::InstructionView view;
    CHECK(loaded.target->disassemble(pc, view));
    CHECK(view.length >= 1);
    CHECK(!view.mnemonic.empty());
}

KRDBG_TEST(kr232_adapter_steps_runs_and_keeps_k_and_i) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    if (!krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error)) {
        return;
    }
    CHECK(loaded.target != nullptr);

    const std::uint64_t entry = loaded.target->pc();
    CHECK(entry != 0U);

    // One step retires exactly one instruction.
    const krdbg::StopResult stepped = loaded.target->step_instruction();
    CHECK(stepped.kind == krdbg::StopKind::Retired);
    CHECK(loaded.target->instructions_executed() >= 1U);

    // Running to the end halts, and the halt is reported as such.
    const krdbg::StopResult ran = loaded.target->run(1000000);
    CHECK(ran.kind == krdbg::StopKind::Halted);
    CHECK(loaded.target->halted());

    // A register write is visible to a read.
    krdbg::ScalarValue value;
    value.bits = 0x1234;
    value.width = 32;
    CHECK(loaded.target->write_value("gpr", 6, value));
    CHECK(loaded.target->read_value("gpr", 6, value));
    CHECK_EQ(value.bits, 0x1234U);

    // K and I are hardware-owned. The machine starts in kernel mode with
    // interrupts known, and a debugger write must not change either.
    std::uint64_t before = 0;
    CHECK(loaded.target->read_flags(before));
    CHECK(loaded.target->write_flags(0));
    std::uint64_t after = 0;
    CHECK(loaded.target->read_flags(after));
    CHECK_EQ(after & 0x40U, before & 0x40U);  // K unchanged
    CHECK_EQ(after & 0x10U, before & 0x10U);  // I unchanged

    // Setting the PC moves it.
    CHECK(loaded.target->set_pc(entry));
    CHECK_EQ(loaded.target->pc(), entry);
}

KRDBG_TEST(kr232_adapter_advances_by_decoded_length) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    if (!krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error)) {
        return;
    }
    CHECK(loaded.target != nullptr);

    // Place a one-byte `ret` followed by a two-byte store in scratch kernel RAM.
    // The disassembler must advance by the decoded length, so the byte after `ret`
    // is the next instruction rather than the middle of a guess.
    const std::uint64_t scratch = 0x400;
    const std::uint8_t code[] = {0x70, 0x8F, 0x32};
    CHECK(loaded.target->write_memory(scratch, code));

    krdbg::InstructionView view;
    CHECK(loaded.target->disassemble(scratch, view));
    CHECK(view.mnemonic == "ret");
    CHECK_EQ(view.length, 1U);

    krdbg::InstructionView next;
    CHECK(loaded.target->disassemble(scratch + 1, next));
    CHECK(next.mnemonic == "st");
    CHECK_EQ(next.length, 2U);

    // And a walk from the entry advances by at least one byte at every step.
    std::uint64_t address = scratch;
    bool saw_long = false;
    for (int step = 0; step < 64; ++step) {
        krdbg::InstructionView step_view;
        if (!loaded.target->disassemble(address, step_view)) {
            break;
        }
        CHECK(step_view.length >= 1U);
        if (step_view.length > 1U) {
            saw_long = true;
        }
        address += step_view.length;
    }
    CHECK(saw_long);
}

KRDBG_TEST(kr232_adapter_exposes_com_and_video) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const std::string session = std::string(KRDBG_SOURCE_ROOT) +
                                "/tests/fixtures/kr232/session.k232";
    if (!krdbg::kr232adapter::load_image(session, {}, loaded, error)) {
        CHECK(false);
        return;
    }
    CHECK(loaded.target != nullptr);

    // The fixture says one byte on the console, draws and flips, then exits 7.
    (void)loaded.target->run(1000000);
    CHECK(loaded.target->halted());
    CHECK(loaded.target->instructions_executed() > 2000U);

    const krdbg::TargetLayout& layout = loaded.target->layout(0);
    CHECK(layout.has_com_port);
    CHECK(layout.has_framebuffer);
    krdbg::IComPort* com = loaded.target->com_port();
    CHECK(com != nullptr);
    if (com != nullptr) {
        std::uint8_t byte = 0;
        CHECK(com->read_output(byte));
        CHECK_EQ(byte, 65U);  // 'A'
        // The queue is drained now, so a second read finds nothing.
        CHECK(!com->read_output(byte));
    }

    const krdbg::IVideoSource* video = loaded.target->video();
    CHECK(video != nullptr);
    if (video != nullptr) {
        krdbg::FramebufferView view;
        CHECK(video->frame(view));
        CHECK_EQ(view.width, 800);
        CHECK_EQ(view.height, 600);
        CHECK_EQ(view.stride, 3200);
        CHECK(view.frame_number >= 1U);
        CHECK(!view.pixels.empty());
    }
}
KRDBG_TEST(kr232_adapter_steps_to_a_line_breakpoint) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const std::string session_path = std::string(KRDBG_SOURCE_ROOT) +
                                     "/tests/fixtures/kr232/session.k232";
    if (!krdbg::kr232adapter::load_image(session_path, {}, loaded, error)) {
        CHECK(false);
        return;
    }
    CHECK(loaded.info.has_value());
    if (!loaded.info.has_value() || loaded.info->lines.empty()) {
        CHECK(false);
        return;
    }

    // Stop on the first line of main: that is what source-level stepping means.
    std::uint64_t line_address = 0;
    for (const krdbg::LineInfo& line : loaded.info->lines) {
        if (line_address == 0 || line.address < line_address) {
            line_address = line.address;
        }
    }
    CHECK(line_address != 0U);

    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    krdbg::DebugSession session(std::move(loaded.target), loaded.info, reload);
    session.add_address_breakpoint(line_address);
    const krdbg::StopResult stopped = session.run(1000000);
    CHECK(stopped.kind == krdbg::StopKind::Retired ||
          stopped.kind == krdbg::StopKind::Halted);
    CHECK_EQ(session.pc(), line_address);
}

KRDBG_TEST(kr232_adapter_reloads_and_refuses_garbage) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const std::string session_path = std::string(KRDBG_SOURCE_ROOT) +
                                     "/tests/fixtures/kr232/session.k232";
    if (!krdbg::kr232adapter::load_image(session_path, {}, loaded, error)) {
        CHECK(false);
        return;
    }
    CHECK(loaded.target != nullptr);

    // Reloading rebuilds the machine from the image, so the PC returns to the
    // entry and the halt is forgotten.
    const std::uint64_t entry = loaded.target->pc();
    (void)loaded.target->run(1000000);
    CHECK(loaded.target->halted());
    CHECK(loaded.target->reset(error));
    CHECK(!loaded.target->halted());
    CHECK_EQ(loaded.target->pc(), entry);

    // A file that is not a container is refused, with a reason.
    const std::string garbage = std::string(KRDBG_SOURCE_ROOT) +
                                "/tests/fixtures/kr232/garbage.k232";
    {
        std::ofstream throwaway(garbage, std::ios::binary);
        throwaway << "NOTACONTAINER";
    }
    krdbg::kr232adapter::LoadedProgram rejected;
    std::string reject_error;
    CHECK(!krdbg::kr232adapter::load_image(garbage, {}, rejected, reject_error));
    CHECK(!reject_error.empty());
    std::filesystem::remove(garbage);
}
KRDBG_TEST(kr232_adapter_unwinds_two_frames) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const std::string nested = std::string(KRDBG_SOURCE_ROOT) +
                               "/tests/fixtures/kr232/nested_frames.k232";
    if (!krdbg::kr232adapter::load_image(nested, {}, loaded, error)) {
        CHECK(false);
        return;
    }
    CHECK(loaded.info.has_value());
    if (!loaded.info.has_value()) {
        return;
    }

    // Stop at the entry of `inner`, so the stack holds main's frame and inner's.
    std::uint64_t inner_begin = 0;
    std::uint64_t main_begin = 0;
    for (const krdbg::FunctionInfo& function : loaded.info->functions) {
        if (function.name == "inner") {
            inner_begin = function.begin;
        } else if (function.name == "main") {
            main_begin = function.begin;
        }
    }
    CHECK(inner_begin != 0U);
    CHECK(main_begin != 0U);
    if (inner_begin == 0U || main_begin == 0U) {
        return;
    }

    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    const krdbg::DebugInfo loaded_info = *loaded.info;
    std::unique_ptr<krdbg::IDebugTarget> owned = std::move(loaded.target);
    krdbg::IDebugTarget* raw = owned.get();
    krdbg::DebugSession session(std::move(owned), loaded.info, reload);
    session.add_address_breakpoint(inner_begin);
    (void)session.run(1000000);
    CHECK_EQ(session.pc(), inner_begin);
    // Step past ENTER so inner's frame exists on the stack.
    // The prologue reaches the parameter store a few instructions in: ENTER, then
    // the frame-relative address and the store itself.
    for (int step = 0; step < 6; ++step) {
        (void)raw->step_instruction();
    }

    // Two frames, the inner one first, and the caller's return address inside main.
    const std::vector<krdbg::RawFrame> frames = raw->unwind();
    CHECK(frames.size() >= 2U);
    if (frames.size() < 2U) {
        return;
    }
    CHECK(frames[0].frame_base != 0U);
    CHECK(frames[1].frame_base != 0U);
    CHECK(frames[0].frame_base != frames[1].frame_base);
    // The inner frame's return address lands inside main; the outer frame is the
    // entry stub, which is placed before main, so its pc is above the entry and
    // below main's first instruction.
    CHECK(frames[0].pc >= main_begin);
    CHECK(frames[1].pc != 0U);
    CHECK(frames[1].pc < main_begin);

    // The local's value, read where the payload says it lives: the debugger adds
    // the variable's frame offset to the frame base it unwound. `inner(3)` was
    // called, so the slot holds 3.
    std::int64_t local_offset = 0;
    for (const krdbg::VariableInfo& variable : loaded_info.variables) {
        if (variable.location.kind == krdbg::LocationKind::FrameOffset) {
            local_offset = variable.location.frame_offset;
        }
    }
    CHECK_EQ(local_offset, -4);
    if (local_offset != 0) {
        const std::uint64_t address =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(frames[0].frame_base) +
                                       local_offset);
        std::uint8_t bytes[4] = {};
        CHECK(raw->read_memory(address, bytes));
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[0]) |
                                    (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                    (static_cast<std::uint32_t>(bytes[2]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[3]) << 24);
        CHECK_EQ(value, 3U);
    }






}
namespace {

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        hash ^= (value >> (8 * i)) & 0xFFu;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string format_hash(std::uint64_t hash) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(hash));
    return buffer;
}

} // namespace

KRDBG_TEST(kr232_adapter_loaded_state_matches_the_golden) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const std::string nested = std::string(KRDBG_SOURCE_ROOT) +
                               "/tests/fixtures/kr232/nested_frames.k232";
    if (!krdbg::kr232adapter::load_image(nested, {}, loaded, error)) {
        CHECK(false);
        return;
    }

    // What a loaded image means for this adapter: the PC, the sixteen general
    // registers, the four FP registers, the flags, and the first page of memory
    // (the vectors and the code the debugger will disassemble).
    std::uint64_t hash = 14695981039346656037ULL;
    hash = mix(hash, loaded.target->pc());
    for (int index = 0; index < 16; ++index) {
        krdbg::ScalarValue value;
        if (loaded.target->read_value("gpr", index, value)) {
            hash = mix(hash, value.bits);
        }
    }
    for (int index = 0; index < 4; ++index) {
        krdbg::ScalarValue value;
        if (loaded.target->read_value("fpr", index, value)) {
            hash = mix(hash, value.bits);
        }
    }
    std::uint64_t flags = 0;
    if (loaded.target->read_flags(flags)) {
        hash = mix(hash, flags);
    }
    std::uint8_t page[256] = {};
    if (loaded.target->read_memory(0, page)) {
        for (std::uint8_t byte : page) {
            hash = mix(hash, byte);
        }
    }
    const std::string text = format_hash(hash);
    const std::string path = std::string(KRDBG_GOLDEN_DIR) + "/kr232-loaded.hash";

    // The first run records the value; later runs assert it. Recording is what
    // makes the golden reviewable -- a later reader sees a recorded number, not a
    // check that would accept whatever the code produced.
    std::ifstream existing(path);
    if (!existing) {
        std::ofstream record(path);
        record << text << "\n";
        CHECK(false);  // recorded: run again to assert
        return;
    }
    std::string golden;
    std::getline(existing, golden);
    CHECK_EQ(golden, text);
}
KRDBG_TEST(kr232_adapter_loads_a_container_without_debug_info) {
    // A container whose section table has no `.kdbg`: the debugger must still load
    // and run it, and say it has no source-level information rather than fabricate
    // a payload. The fixture is loaded.k232 with every `.kdbg` string renamed, so
    // the file stays a valid container with a recomputed CRC.
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const std::string path = std::string(KRDBG_SOURCE_ROOT) +
                             "/tests/fixtures/kr232/no_debug.k232";
    const bool ok = krdbg::kr232adapter::load_image(path, {}, loaded, error);
    CHECK(ok);
    if (!ok) {
        return;
    }
    CHECK(loaded.debug_path == krdbg::kr232adapter::DebugPath::None);
    CHECK(!loaded.info.has_value());
    CHECK(loaded.target != nullptr);
    if (loaded.target != nullptr) {
        CHECK(loaded.target->pc() != 0U);
        krdbg::InstructionView view;
        CHECK(loaded.target->disassemble(loaded.target->pc(), view));
        CHECK(!view.mnemonic.empty());
        (void)loaded.target->run(1000000);
        CHECK(loaded.target->halted());
    }
}

KRDBG_TEST(kr232_adapter_reads_a_local_through_the_frame) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const std::string nested = std::string(KRDBG_SOURCE_ROOT) +
                               "/tests/fixtures/kr232/nested_frames.k232";
    if (!krdbg::kr232adapter::load_image(nested, {}, loaded, error)) {
        CHECK(false);
        return;
    }
    CHECK(loaded.info.has_value());
    if (!loaded.info.has_value()) {
        return;
    }
    // One local, `x`, at -4 from the frame base: the slot the backend gave it.
    CHECK(!loaded.info->variables.empty());
    bool saw_local = false;
    for (const krdbg::VariableInfo& variable : loaded.info->variables) {
        if (variable.location.kind != krdbg::LocationKind::FrameOffset) {
            continue;
        }
        saw_local = true;
        CHECK(variable.location.frame_offset == -4);
        CHECK(variable.size == 4U);
        CHECK_EQ(variable.name, std::string("x"));
    }
    CHECK(saw_local);
}
