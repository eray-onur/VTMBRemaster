#pragma once

#include "openbl/rhi/rhi.hpp"

#include <memory>

namespace openbl::platform {
class Window;
}

namespace openbl::rhi {

struct VulkanOptions {
    // Enables VK_LAYER_KHRONOS_validation if it is installed.
    bool validation = false;
    bool vsync = true;
};

// Throws std::runtime_error if no suitable Vulkan 1.3 device is available.
std::unique_ptr<Device> create_vulkan_device(platform::Window& window, const VulkanOptions& options);

} // namespace openbl::rhi
