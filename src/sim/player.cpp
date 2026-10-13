#include "openbl/sim/player.hpp"

#include "openbl/sim/world.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::sim {

namespace {

using formats::Vec3;

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// Walkable ground: surfaces facing up at least this much (Source's 0.7).
constexpr float kFloorNormal = 0.7f;

formats::BspEntityPairs player_keyvalues() {
    return {{"classname", "player"}};
}

} // namespace

Player::Player(EntityId id, const Vec3& feet) : Entity(id, player_keyvalues()), feet_(feet) {}

bool Player::teleport(World& world, const Vec3& feet) {
    feet_ = feet;
    velocity_ = {};
    bool room = false;
    for (int lift = 0; lift <= 64 && !room; ++lift) {
        const Vec3 p{feet[0], feet[1], feet[2] + static_cast<float>(lift)};
        if (!world.trace(p, p, mins(), maxs(), contents::PlayerSolid, id()).start_solid) {
            feet_ = p;
            room = true;
        }
    }
    categorize(world);
    return room;
}

void Player::categorize(World& world) {
    const Trace t = world.trace(feet_, {feet_[0], feet_[1], feet_[2] - 2.0f}, mins(), maxs(), contents::PlayerSolid, id());
    on_ground_ = velocity_[2] <= 140.0f && t.fraction < 1 && t.normal[2] >= kFloorNormal;
}

void Player::move(World& world, const UserCommand& cmd, float dt) {
    if (dt <= 0) {
        return;
    }
    dt = std::min(dt, 0.1f);
    const MoveSettings& s = settings;

    // Crouching: the hull shrinks from the top; standing needs room for it.
    if (cmd.crouch && !crouched_) {
        crouched_ = true;
    } else if (!cmd.crouch && crouched_) {
        const Vec3 tall{16, 16, 72};
        if (!world.trace(feet_, feet_, mins(), tall, contents::PlayerSolid, id()).start_solid) {
            crouched_ = false;
        }
    }

    categorize(world);

    if (cmd.jump && !jump_held_ && on_ground_) {
        velocity_[2] = s.jump_speed;
        on_ground_ = false;
    }
    jump_held_ = cmd.jump;

    // Friction on the ground.
    if (on_ground_) {
        const float speed = std::sqrt(dot(velocity_, velocity_));
        if (speed > 0.1f) {
            const float control = std::max(speed, s.stop_speed);
            const float slowed = std::max(speed - control * s.friction * dt, 0.0f);
            for (float& v : velocity_) {
                v *= slowed / speed;
            }
        }
    }

    // Where the controls point, on the ground plane.
    const float yaw = cmd.yaw * 3.14159265f / 180.0f;
    const Vec3 forward{std::cos(yaw), std::sin(yaw), 0};
    const Vec3 right{std::sin(yaw), -std::cos(yaw), 0};
    const float f = cmd.forward < 0 ? cmd.forward * s.back_scale : cmd.forward;
    Vec3 wish{forward[0] * f + right[0] * cmd.side, forward[1] * f + right[1] * cmd.side, 0};
    const float amount = std::min(std::sqrt(dot(wish, wish)), 1.0f);
    if (amount > 0) {
        const float len = std::sqrt(dot(wish, wish));
        wish = {wish[0] / len, wish[1] / len, 0};
    }
    const float max_speed = crouched_ ? s.crouch_speed : cmd.walk ? s.walk_speed : s.run_speed;
    float wish_speed = max_speed * amount;

    // Accelerate towards it (in the air only a little).
    const float accel = on_ground_ ? s.accelerate : s.air_accelerate;
    const float capped = on_ground_ ? wish_speed : std::min(wish_speed, s.air_speed_cap);
    const float add = capped - dot(velocity_, wish);
    if (add > 0 && amount > 0) {
        const float gain = std::min(accel * dt * wish_speed, add);
        velocity_[0] += gain * wish[0];
        velocity_[1] += gain * wish[1];
    }

    if (on_ground_) {
        velocity_[2] = 0;
        walk(world, dt);
        stay_on_ground(world);
    } else {
        velocity_[2] -= s.gravity * dt;
        slide(world, dt);
    }
    categorize(world);
    if (on_ground_) {
        velocity_[2] = 0;
    }
    world.update_touches();
}

void Player::slide(World& world, float dt) {
    std::array<Vec3, 5> planes{};
    int count = 0;
    const Vec3 original = velocity_;
    float time = dt;
    for (int bump = 0; bump < 4 && time > 0; ++bump) {
        const Vec3 end{feet_[0] + velocity_[0] * time, feet_[1] + velocity_[1] * time, feet_[2] + velocity_[2] * time};
        const Trace t = world.trace(feet_, end, mins(), maxs(), contents::PlayerSolid, id());
        if (t.all_solid) {
            velocity_ = {};
            return;
        }
        if (t.fraction > 0) {
            feet_ = t.end;
            count = 0;
        }
        if (t.fraction == 1) {
            return;
        }
        time -= time * t.fraction;
        if (count >= static_cast<int>(planes.size())) {
            velocity_ = {};
            return;
        }
        planes[static_cast<std::size_t>(count++)] = t.normal;
        // A velocity that slides along every plane hit so far, or along the
        // crease of two of them; none: stop.
        int i = 0;
        for (; i < count; ++i) {
            Vec3 v = original;
            const float back = dot(v, planes[static_cast<std::size_t>(i)]);
            for (int k = 0; k < 3; ++k) {
                v[k] -= planes[static_cast<std::size_t>(i)][k] * back;
            }
            bool ok = true;
            for (int j = 0; j < count; ++j) {
                if (j != i && dot(v, planes[static_cast<std::size_t>(j)]) < 0) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                velocity_ = v;
                break;
            }
        }
        if (i == count) {
            if (count != 2) {
                velocity_ = {};
                return;
            }
            Vec3 dir = cross(planes[0], planes[1]);
            const float len = std::sqrt(dot(dir, dir));
            if (len < 1e-6f) {
                velocity_ = {};
                return;
            }
            dir = {dir[0] / len, dir[1] / len, dir[2] / len};
            const float along = dot(dir, velocity_);
            velocity_ = {dir[0] * along, dir[1] * along, dir[2] * along};
        }
        // Never bounce back against where it was going.
        if (dot(velocity_, original) <= 0) {
            velocity_ = {};
            return;
        }
    }
}

void Player::walk(World& world, float dt) {
    const Vec3 start = feet_;
    const Vec3 start_velocity = velocity_;
    // Along the ground.
    slide(world, dt);
    const Vec3 down = feet_;
    const Vec3 down_velocity = velocity_;
    // A step up, across, and back down.
    feet_ = start;
    velocity_ = start_velocity;
    const Trace up = world.trace(feet_, {feet_[0], feet_[1], feet_[2] + settings.step_size}, mins(), maxs(),
                                 contents::PlayerSolid, id());
    feet_ = up.end;
    slide(world, dt);
    const Trace land = world.trace(feet_, {feet_[0], feet_[1], feet_[2] - settings.step_size - 1.0f}, mins(), maxs(),
                                   contents::PlayerSolid, id());
    feet_ = land.end;
    const bool stepped = land.fraction < 1 && land.normal[2] >= kFloorNormal && !land.all_solid;
    const float down_dist = (down[0] - start[0]) * (down[0] - start[0]) + (down[1] - start[1]) * (down[1] - start[1]);
    const float up_dist = (feet_[0] - start[0]) * (feet_[0] - start[0]) + (feet_[1] - start[1]) * (feet_[1] - start[1]);
    if (!stepped || down_dist >= up_dist) {
        feet_ = down;
        velocity_ = down_velocity;
    } else {
        velocity_[2] = down_velocity[2];
    }
}

void Player::stay_on_ground(World& world) {
    // Down stairs and slopes: if ground is within a step below, stand on it.
    const Vec3 above{feet_[0], feet_[1], feet_[2] + 2.0f};
    const Trace t = world.trace(above, {feet_[0], feet_[1], feet_[2] - settings.step_size}, mins(), maxs(),
                                contents::PlayerSolid, id());
    if (t.fraction > 0 && t.fraction < 1 && !t.start_solid && t.normal[2] >= kFloorNormal) {
        feet_ = t.end;
    }
}

} // namespace openbl::sim
