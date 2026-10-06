#pragma once

// Free-fly camera in Source coordinates: +Z up, yaw around Z, pitch up positive.

#include "openbl/platform/window.hpp"

#include <glm/glm.hpp>

namespace openbl::viewer {

struct Camera {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;   // Degrees; 0 looks along +X.
    float pitch = 0.0f; // Degrees.
    float speed = 400.0f; // Units per second.
    float fov = 75.0f;

    glm::vec3 forward() const;
    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;

    // Right mouse: look. WASD: move. E/Q or Space/Ctrl: up/down. Shift: fast. Wheel: speed.
    void update(const platform::Input& input, float dt);
};

} // namespace openbl::viewer
