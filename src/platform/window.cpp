#include "openbl/platform/window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdexcept>

namespace openbl::platform {

namespace {

int key_index(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_W: return static_cast<int>(Key::W);
    case SDL_SCANCODE_A: return static_cast<int>(Key::A);
    case SDL_SCANCODE_S: return static_cast<int>(Key::S);
    case SDL_SCANCODE_D: return static_cast<int>(Key::D);
    case SDL_SCANCODE_Q: return static_cast<int>(Key::Q);
    case SDL_SCANCODE_E: return static_cast<int>(Key::E);
    case SDL_SCANCODE_C: return static_cast<int>(Key::C);
    case SDL_SCANCODE_P: return static_cast<int>(Key::P);
    case SDL_SCANCODE_SPACE: return static_cast<int>(Key::Space);
    case SDL_SCANCODE_LCTRL: return static_cast<int>(Key::Ctrl);
    case SDL_SCANCODE_LSHIFT: return static_cast<int>(Key::Shift);
    case SDL_SCANCODE_ESCAPE: return static_cast<int>(Key::Escape);
    case SDL_SCANCODE_TAB: return static_cast<int>(Key::Tab);
    case SDL_SCANCODE_F1: return static_cast<int>(Key::F1);
    case SDL_SCANCODE_F2: return static_cast<int>(Key::F2);
    case SDL_SCANCODE_F3: return static_cast<int>(Key::F3);
    case SDL_SCANCODE_F4: return static_cast<int>(Key::F4);
    default: return -1;
    }
}

int button_index(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return static_cast<int>(MouseButton::Left);
    case SDL_BUTTON_RIGHT: return static_cast<int>(MouseButton::Right);
    case SDL_BUTTON_MIDDLE: return static_cast<int>(MouseButton::Middle);
    default: return -1;
    }
}

} // namespace

Window::Window(const WindowDesc& desc) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    window_ = SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.width), static_cast<int>(desc.height),
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed: " + error);
    }
}

Window::~Window() {
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Window::poll_events() {
    input_.pressed_.fill(false);
    input_.mouse_dx_ = 0;
    input_.mouse_dy_ = 0;
    input_.wheel_ = 0;
    bool keep_running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            keep_running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const int k = key_index(event.key.scancode);
            if (k >= 0) {
                const bool down = event.type == SDL_EVENT_KEY_DOWN;
                if (down && !event.key.repeat) {
                    input_.pressed_[static_cast<std::size_t>(k)] = true;
                }
                input_.keys_[static_cast<std::size_t>(k)] = down;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int b = button_index(event.button.button);
            if (b >= 0) {
                input_.buttons_[static_cast<std::size_t>(b)] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            input_.mouse_dx_ += event.motion.xrel;
            input_.mouse_dy_ += event.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            input_.wheel_ += event.wheel.y;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            input_.keys_.fill(false);
            input_.buttons_.fill(false);
            break;
        default:
            break;
        }
    }
    return keep_running;
}

std::pair<std::uint32_t, std::uint32_t> Window::framebuffer_size() const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    return {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
}

bool Window::minimized() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}

void Window::set_title(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

void Window::set_relative_mouse(bool enabled) {
    SDL_SetWindowRelativeMouseMode(window_, enabled);
}

bool Window::set_clipboard_text(const std::string& text) {
    return SDL_SetClipboardText(text.c_str());
}

std::vector<const char*> Window::vulkan_instance_extensions() const {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    return {names, names + count};
}

std::uint64_t Window::create_vulkan_surface(void* vk_instance) const {
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(window_, static_cast<VkInstance>(vk_instance), nullptr, &surface)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    return reinterpret_cast<std::uint64_t>(surface);
}

} // namespace openbl::platform
