#pragma once

// The player: a box that walks the map's collision, moved each frame by a
// command from the client (Source's ground movement: friction, acceleration
// towards the wished direction, gravity, sliding along walls, stepping up
// stairs and staying on the ground going down them, jumping and crouching).
//
// The numbers are Source's defaults (sv_gravity 800, sv_friction 4,
// sv_accelerate 10, sv_stepsize 18); the run and walk speeds are provisional
// until measured against the original (docs/ROADMAP.md).

#include "openbl/sim/entity.hpp"

namespace openbl::sim {

// One frame of the client's controls.
struct UserCommand {
    float forward = 0; // -1..1
    float side = 0;    // -1..1, right positive.
    float yaw = 0;     // Degrees; where "forward" points.
    bool jump = false;
    bool crouch = false;
    bool walk = false; // Slow walk instead of running.
};

struct MoveSettings {
    float gravity = 800;
    float friction = 4;
    float stop_speed = 100;
    float accelerate = 10;
    float air_accelerate = 10;
    float air_speed_cap = 30; // How much the air control may add (Source's 30).
    float step_size = 18;
    float run_speed = 190;
    float walk_speed = 90;
    float crouch_speed = 70;
    float back_scale = 0.6f; // sv_backspeed.
    float jump_speed = 268;  // About 45 units high.
};

class Player final : public Entity {
public:
    Player(EntityId id, const formats::Vec3& feet);

    // Runs one command for `dt` seconds.
    void move(World& world, const UserCommand& cmd, float dt);
    // Puts the player at `feet`, lifted out of anything solid (up to 64
    // units); false if there was no room (it is then left at `feet`).
    bool teleport(World& world, const formats::Vec3& feet);

    const formats::Vec3& feet() const { return feet_; }
    const formats::Vec3& velocity() const { return velocity_; }
    formats::Vec3 eye() const { return {feet_[0], feet_[1], feet_[2] + (crouched_ ? 28.0f : 64.0f)}; }
    bool on_ground() const { return on_ground_; }
    bool crouched() const { return crouched_; }
    formats::Vec3 mins() const { return {-16, -16, 0}; }
    formats::Vec3 maxs() const { return {16, 16, crouched_ ? 36.0f : 72.0f}; }

    MoveSettings settings;

private:
    void categorize(World& world);
    void slide(World& world, float dt); // TryPlayerMove: move, clipping velocity against what it hits.
    void walk(World& world, float dt);  // Slide, and also try it a step up; keep the farther.
    void stay_on_ground(World& world);

    formats::Vec3 feet_;
    formats::Vec3 velocity_{};
    bool on_ground_ = false;
    bool crouched_ = false;
    bool jump_held_ = false;
};

} // namespace openbl::sim
