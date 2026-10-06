#include "camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace openbl::viewer {

using platform::Key;

glm::vec3 Camera::forward() const {
    const float y = glm::radians(yaw);
    const float p = glm::radians(pitch);
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + forward(), glm::vec3(0, 0, 1));
}

glm::mat4 Camera::projection(float aspect) const {
    glm::mat4 p = glm::perspective(glm::radians(fov), aspect, 2.0f, 65536.0f);
    p[1][1] *= -1.0f; // Vulkan clip space has +Y down.
    return p;
}

void Camera::update(const platform::Input& input, float dt) {
    if (input.down(platform::MouseButton::Right)) {
        constexpr float kSensitivity = 0.15f;
        yaw -= input.mouse_dx() * kSensitivity;
        pitch = std::clamp(pitch - input.mouse_dy() * kSensitivity, -89.0f, 89.0f);
    }
    if (input.wheel() != 0) {
        speed = std::clamp(speed * std::pow(1.25f, input.wheel()), 25.0f, 10000.0f);
    }
    const glm::vec3 f = forward();
    const glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 0, 1)));
    glm::vec3 move(0.0f);
    if (input.down(Key::W)) move += f;
    if (input.down(Key::S)) move -= f;
    if (input.down(Key::D)) move += right;
    if (input.down(Key::A)) move -= right;
    if (input.down(Key::E) || input.down(Key::Space)) move.z += 1;
    if (input.down(Key::Q) || input.down(Key::Ctrl)) move.z -= 1;
    if (glm::dot(move, move) > 0) {
        const float boost = input.down(Key::Shift) ? 4.0f : 1.0f;
        position += glm::normalize(move) * speed * boost * dt;
    }
}

} // namespace openbl::viewer
