#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "krdbg/target.h"

namespace krdbg {

// The fake COM port follows the three-register contract: OUT holds guest
// output until the debugger consumes it, IN holds one debugger byte until the
// guest reads it, and a second write_input is refused and reports overrun.
class FakeComPort final : public IComPort {
public:
    std::string name() const override { return "COM1"; }
    ComPortStatus status() const override;
    bool read_output(std::uint8_t& byte) override;
    bool write_input(std::uint8_t byte) override;
    void clear_overrun() override;

    void push_output(std::uint8_t byte);
    void push_output(std::string_view text);
    void reset();
    std::optional<std::uint8_t> input_byte() const { return input_; }
    bool take_input();

private:
    std::deque<std::uint8_t> output_;
    std::optional<std::uint8_t> input_;
    bool overrun_ = false;
};

// A deterministic framebuffer. set_mode() rebuilds a horizontal/vertical
// gradient and advances the frame number; it never depends on wall time.
class FakeVideoSource final : public IVideoSource {
public:
    FakeVideoSource();

    bool frame(FramebufferView& out) const override;
    void set_mode(int width, int height);
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    void rebuild();

    int width_ = 320;
    int height_ = 240;
    std::uint64_t frame_number_ = 1;
    std::vector<std::uint8_t> pixels_;
};

// A deterministic target for M0's visual baseline and M1's session tests. It
// models a small 32-bit machine: eight GPRs, PC/SP/FP/LR, four flags, 64 KiB of
// memory, a fixed disassembly table, a three-frame unwind and both optional
// devices.
class FakeTarget final : public IDebugTarget {
public:
    static constexpr std::uint64_t kMemoryBase = 0x1000;
    static constexpr std::uint64_t kMemorySize = 0x10000;

    FakeTarget();

    // Test and demo configuration.
    void add_context(std::string name);
    void set_halted(bool halted) { halted_ = halted; }
    void set_register(int index, std::uint64_t value);
    [[nodiscard]] std::uint64_t register_value(int index) const;
    void set_frame_base(std::uint64_t base);
    void set_callers(std::vector<RawFrame> callers);
    void set_step_stuck(bool stuck) { stuck_ = stuck; }
    FakeComPort& com() { return com_; }
    FakeVideoSource& video_source() { return video_; }
    void set_instructions_executed(std::uint64_t count) { instructions_ = count; }

    // IDebugTarget.
    const TargetLayout& layout(std::uint32_t context) const override;
    std::vector<ContextInfo> contexts() const override;
    bool select_context(std::uint32_t id) override;
    std::uint32_t current_context() const override;

    StopResult step_instruction() override;
    StopResult run(std::uint64_t budget) override;
    bool halted() const override { return halted_; }
    std::uint64_t pc() const override;
    bool set_pc(std::uint64_t pc) override;
    std::uint64_t instructions_executed() const override { return instructions_; }
    bool reset(std::string& error) override;

    bool read_value(std::string_view group, int index, ScalarValue& value) const override;
    bool write_value(std::string_view group, int index, ScalarValue value) override;
    bool read_flags(std::uint64_t& bits) const override;
    bool write_flags(std::uint64_t bits) override;
    bool read_matrix(std::string_view group, int row, int column,
                     float& value) const override;
    bool write_matrix(std::string_view group, int row, int column, float value) override;

    bool read_memory(std::uint64_t address, std::span<std::uint8_t> out) const override;
    bool write_memory(std::uint64_t address, std::span<const std::uint8_t> in) override;
    bool disassemble(std::uint64_t address, InstructionView& out) const override;
    std::vector<RawFrame> unwind() const override;

    IComPort* com_port() override { return &com_; }
    const IVideoSource* video() const override { return &video_; }

private:
    struct Context {
        std::uint32_t id = 0;
        std::string name;
        std::uint64_t pc = 0;
        std::uint64_t sp = 0;
        std::uint64_t fp = 0;
        std::uint64_t lr = 0;
        std::uint64_t acc = 0;
        std::array<std::uint64_t, 8> registers{};
        std::vector<RawFrame> callers;  // return pc and caller frame base
    };

    Context& current();
    const Context& current() const;
    void initialize_memory();

    TargetLayout layout_;
    std::vector<Context> contexts_;
    std::uint32_t current_id_ = 1;
    std::uint64_t flags_ = 0;
    std::uint64_t instructions_ = 0;
    bool halted_ = false;
    bool stuck_ = false;
    std::uint64_t entry_ = 0x1000;
    std::uint64_t frame_base_ = 0x2100;
    std::vector<std::uint8_t> memory_;
    FakeComPort com_;
    FakeVideoSource video_;
};

} // namespace krdbg
