#pragma once

// Native window and input, backed by SDL3. Graphics-API handles are passed as
// opaque integers so that this header does not depend on Vulkan or D3D.

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;

namespace openbl::platform {

enum class Key : std::uint8_t {
    W, A, S, D, Q, E, C, P, V, Space, Ctrl, Shift, Escape, Tab, F1, F2, F3, F4,
    Count
};

enum class MouseButton : std::uint8_t { Left, Right, Middle, Count };

class Input {
public:
    bool down(Key k) const { return keys_[static_cast<std::size_t>(k)]; }
    bool pressed(Key k) const { return pressed_[static_cast<std::size_t>(k)]; }
    bool down(MouseButton b) const { return buttons_[static_cast<std::size_t>(b)]; }
    // Mouse movement since the previous poll, in pixels.
    float mouse_dx() const { return mouse_dx_; }
    float mouse_dy() const { return mouse_dy_; }
    float wheel() const { return wheel_; }

private:
    friend class Window;
    std::array<bool, static_cast<std::size_t>(Key::Count)> keys_{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> pressed_{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> buttons_{};
    float mouse_dx_ = 0;
    float mouse_dy_ = 0;
    float wheel_ = 0;
};

struct WindowDesc {
    std::string title = "OpenBL";
    std::uint32_t width = 1600;
    std::uint32_t height = 900;
};

class Window {
public:
    // Throws std::runtime_error.
    explicit Window(const WindowDesc& desc);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Processes pending events. Returns false once the user asked to quit.
    bool poll_events();

    const Input& input() const { return input_; }
    std::pair<std::uint32_t, std::uint32_t> framebuffer_size() const;
    bool minimized() const;
    void set_title(const std::string& title);
    // Hides the cursor and reports unbounded relative motion while enabled.
    void set_relative_mouse(bool enabled);
    // Puts UTF-8 text on the system clipboard. Returns false on failure.
    bool set_clipboard_text(const std::string& text);

    // Vulkan integration.
    std::vector<const char*> vulkan_instance_extensions() const;
    // Returns a VkSurfaceKHR for the given VkInstance. Throws on failure.
    std::uint64_t create_vulkan_surface(void* vk_instance) const;

private:
    SDL_Window* window_ = nullptr;
    Input input_;
};

} // namespace openbl::platform
