#include "krdbg/window.h"

#include <string>
#include <string_view>

namespace krdbg::platform {
namespace {

// Headless presenter: tests and --headless scenarios never create an OS window.
// present() deliberately keeps no copy; the caller owns the Canvas.
class NullWindow final : public IWindow {
public:
    bool create(std::string_view title, int width, int height) override {
        title_ = std::string(title);
        width_ = width;
        height_ = height;
        created_ = true;
        return true;
    }
    void destroy() override { created_ = false; }
    [[nodiscard]] bool valid() const override { return created_; }

    bool poll(Event& event) override {
        (void)event;
        return false;
    }
    void present(const Canvas& canvas) override {
        (void)canvas;
    }
    void set_title(std::string_view title) override { title_ = std::string(title); }
    void invalidate() override {}

private:
    bool created_ = false;
    std::string title_;
    int width_ = 0;
    int height_ = 0;
};

class NullVideoWindow final : public IVideoWindow {
public:
    bool create(std::string_view title, int width, int height) override {
        title_ = std::string(title);
        width_ = width;
        height_ = height;
        created_ = true;
        return true;
    }
    void destroy() override { created_ = false; }
    [[nodiscard]] bool valid() const override { return created_; }
    bool poll(Event&) override { return false; }
    void present(const FramebufferView&, bool) override {}
    void set_title(std::string_view title) override { title_ = std::string(title); }

private:
    bool created_ = false;
    std::string title_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace

std::unique_ptr<IWindow> create_window() {
    return std::make_unique<NullWindow>();
}

std::unique_ptr<IVideoWindow> create_video_window() {
    return std::make_unique<NullVideoWindow>();
}

bool open_file_dialog(std::string& path) {
    (void)path;
    return false;
}

std::string clipboard_text() {
    return {};
}

} // namespace krdbg::platform
