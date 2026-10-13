// Entity classes that move: doors that slide or swing, spinning brushes, and
// the area portals doors open and close.

#include "classes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace openbl::sim {

namespace {

// Movement is stepped at the original's tick rate.
constexpr double kMoveStep = 1.0 / 66.0;

// Source's angle convention: pitch down positive, yaw about +z.
formats::Vec3 angle_forward(const formats::Vec3& angles) {
    const float p = angles[0] * 3.14159265f / 180.0f, y = angles[1] * 3.14159265f / 180.0f;
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
}

formats::Vec3 parse_vec3(std::string_view text) {
    formats::Vec3 v{};
    const std::string s(text);
    std::sscanf(s.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
    return v;
}

// A door: moves between its closed position (0) and its open one (1) at a
// constant speed. Open, Close and Toggle move it; it closes again `wait`
// seconds after opening fully (-1: it stays). Fires OnOpen and OnClose when
// it starts to move, OnFullyOpen and OnFullyClosed when it arrives. Opening
// opens the area portals that name it as their target; closing fully closes
// them. A "linked_door" opens and closes with it. Lock and Unlock only stop
// the player using it (the original's inputs still move a locked door).
class Door : public Entity {
public:
    Door(EntityId id, formats::BspEntityPairs kv) : Entity(id, std::move(kv)) {
        speed_ = std::max(number("speed", 100.0f), 1e-3f);
        wait_ = (spawnflags() & kToggle) ? -1.0f : number("wait", 4.0f);
        locked_ = (spawnflags() & kStartsLocked) != 0;
        if (spawnflags() & kStartsOpen) {
            position_ = 1.0;
            target_ = 1.0;
        }
    }

    void spawn(World& world) override {
        travel_ = travel(world);
        update_motion(world);
    }
    // After every portal has spawned (they set their own start state).
    void activate(World& world) override {
        if (position_ > 0) {
            set_portals(world, true);
        }
    }

    bool input(World& world, std::string_view name, std::string_view, EntityId activator, EntityId) override {
        if (iequals(name, "Open")) {
            move(world, 1.0, activator);
        } else if (iequals(name, "Close")) {
            move(world, 0.0, activator);
        } else if (iequals(name, "Toggle")) {
            move(world, target_ > 0.5 ? 0.0 : 1.0, activator);
        } else if (iequals(name, "Lock")) {
            locked_ = true;
        } else if (iequals(name, "Unlock")) {
            locked_ = false;
        } else {
            return false;
        }
        return true;
    }

    void think(World& world) override {
        if (position_ == target_) {
            // Waited out the time open: close again.
            move(world, 0.0, activator_);
            return;
        }
        const double step = kMoveStep * speed_ / std::max(travel_, 1e-3);
        position_ = target_ > position_ ? std::min(position_ + step, target_) : std::max(position_ - step, target_);
        update_motion(world);
        if (position_ != target_) {
            set_next_think(world.time() + kMoveStep);
            return;
        }
        if (target_ == 1.0) {
            fire_output(world, "OnFullyOpen", activator_);
            if (wait_ >= 0) {
                set_next_think(world.time() + wait_);
            }
        } else {
            fire_output(world, "OnFullyClosed", activator_);
            set_portals(world, false);
        }
    }

    bool locked() const { return locked_; }

protected:
    static constexpr int kStartsOpen = 1;
    static constexpr int kToggle = 32;
    static constexpr int kStartsLocked = 2048;

    // How far the door moves between its two positions (units or degrees).
    virtual double travel(World& world) const = 0;
    // The motion at `position` (0 closed, 1 open).
    virtual Transform motion_at(double position) const = 0;

private:
    void move(World& world, double target, EntityId activator) {
        if (target == target_) {
            return;
        }
        target_ = target;
        activator_ = activator;
        if (target == 1.0) {
            fire_output(world, "OnOpen", activator);
            set_portals(world, true);
        } else {
            fire_output(world, "OnClose", activator);
        }
        if (const std::string_view linked = value("linked_door"); !linked.empty()) {
            world.queue_input(linked, target == 1.0 ? "Open" : "Close", "", 0.0, activator, id());
        }
        set_next_think(world.time() + kMoveStep);
    }

    void update_motion(World& world) {
        set_motion(world, position_ > 0 ? std::optional<Transform>(motion_at(position_)) : std::nullopt);
    }

    void set_portals(World& world, bool open) {
        if (targetname().empty()) {
            return;
        }
        for (EntityId id : world.find("func_areaportal")) {
            const Entity* portal = world.entity(id);
            if (iequals(portal->value("target"), targetname())) {
                world.set_portal_open(static_cast<int>(portal->number("portalnumber")), open);
            }
        }
    }

    double speed_;
    double travel_ = 0;
    float wait_;
    bool locked_;
    double position_ = 0.0; // 0 closed, 1 open.
    double target_ = 0.0;
    EntityId activator_ = kNoEntity;
};

// func_door: slides along its "angles" direction by its size along that
// direction, less "lip", as Source's doors do (2 units of the size overlap).
class FuncDoor final : public Door {
public:
    using Door::Door;

protected:
    double travel(World& world) const override {
        dir_ = angle_forward(parse_vec3(value("angles")));
        const std::string_view model = value("model");
        formats::Vec3 size{};
        if (model.size() > 1 && model[0] == '*') {
            const auto index = static_cast<std::size_t>(std::strtoul(std::string(model.substr(1)).c_str(), nullptr, 10));
            if (index < world.bsp().models.size()) {
                const auto& m = world.bsp().models[index];
                size = {m.maxs[0] - m.mins[0], m.maxs[1] - m.mins[1], m.maxs[2] - m.mins[2]};
            }
        }
        double d = 0;
        for (int k = 0; k < 3; ++k) {
            d += std::abs(dir_[k] * (size[k] - 2.0f));
        }
        distance_ = static_cast<float>(std::max(d - number("lip"), 0.0));
        return distance_;
    }
    Transform motion_at(double position) const override {
        const float d = static_cast<float>(position) * distance_;
        return Transform::translate({dir_[0] * d, dir_[1] * d, dir_[2] * d});
    }

private:
    mutable formats::Vec3 dir_{};
    mutable float distance_ = 0;
};

// func_door_rotating: swings "distance" degrees about its origin: about z, or
// x with spawnflag 64, or y with 128; spawnflag 2 swings the other way.
class FuncDoorRotating final : public Door {
public:
    using Door::Door;

protected:
    double travel(World&) const override {
        axis_ = (spawnflags() & 64) ? formats::Vec3{1, 0, 0} : (spawnflags() & 128) ? formats::Vec3{0, 1, 0}
                                                                                     : formats::Vec3{0, 0, 1};
        degrees_ = number("distance", 90.0f) * ((spawnflags() & 2) ? -1.0f : 1.0f);
        pivot_ = origin();
        return std::abs(degrees_);
    }
    Transform motion_at(double position) const override {
        return Transform::rotate(axis_, static_cast<float>(position) * degrees_, pivot_);
    }

private:
    mutable formats::Vec3 axis_{0, 0, 1};
    mutable float degrees_ = 90;
    mutable formats::Vec3 pivot_{};
};

// func_rotating: spins about its origin at "maxspeed" degrees a second while
// on (spawnflag 1 starts it): about z, or x with spawnflag 4, or y with 8;
// spawnflag 2 reverses it. Start, Stop, Toggle, Reverse, SetSpeed (a fraction
// of maxspeed). The original eases speed changes by "fanfriction"; this
// changes it at once.
class FuncRotating final : public Entity {
public:
    FuncRotating(EntityId id, formats::BspEntityPairs kv) : Entity(id, std::move(kv)) {
        axis_ = (spawnflags() & 4) ? formats::Vec3{1, 0, 0} : (spawnflags() & 8) ? formats::Vec3{0, 1, 0}
                                                                                 : formats::Vec3{0, 0, 1};
        max_speed_ = number("maxspeed", 100.0f) * ((spawnflags() & 2) ? -1.0f : 1.0f);
        speed_ = (spawnflags() & 1) ? max_speed_ : 0.0f;
    }
    void spawn(World& world) override {
        pivot_ = origin();
        if (speed_ != 0) {
            set_next_think(world.time() + kMoveStep);
        }
    }
    void think(World& world) override {
        angle_ = std::fmod(angle_ + speed_ * kMoveStep, 360.0);
        set_motion(world, Transform::rotate(axis_, static_cast<float>(angle_), pivot_));
        if (speed_ != 0) {
            set_next_think(world.time() + kMoveStep);
        }
    }
    bool input(World& world, std::string_view name, std::string_view parameter, EntityId, EntityId) override {
        if (iequals(name, "Start")) {
            speed_ = max_speed_;
        } else if (iequals(name, "Stop")) {
            speed_ = 0;
        } else if (iequals(name, "Toggle")) {
            speed_ = speed_ != 0 ? 0.0f : max_speed_;
        } else if (iequals(name, "Reverse")) {
            max_speed_ = -max_speed_;
            speed_ = -speed_;
        } else if (iequals(name, "SetSpeed")) {
            speed_ = max_speed_ * std::strtof(std::string(parameter).c_str(), nullptr);
        } else {
            return false;
        }
        set_next_think(speed_ != 0 ? std::optional<double>(world.time() + kMoveStep) : std::nullopt);
        return true;
    }

private:
    formats::Vec3 axis_;
    formats::Vec3 pivot_{};
    float max_speed_;
    float speed_;
    double angle_ = 0;
};

// func_areaportal: its portal starts closed unless StartOpen is 1 (or the key
// is missing, the editor's default); Open and Close set it. Doors naming it
// set it too.
class FuncAreaPortal final : public Entity {
public:
    using Entity::Entity;
    void spawn(World& world) override {
        const std::string_view start = value("StartOpen");
        world.set_portal_open(portal(), start.empty() || start == "1");
    }
    bool input(World& world, std::string_view name, std::string_view, EntityId, EntityId) override {
        if (iequals(name, "Open") || iequals(name, "Close")) {
            world.set_portal_open(portal(), iequals(name, "Open"));
            return true;
        }
        if (iequals(name, "Toggle")) {
            world.set_portal_open(portal(), !world.portal_open(portal()));
            return true;
        }
        return false;
    }

private:
    int portal() const { return static_cast<int>(number("portalnumber")); }
};

} // namespace

void add_mover_classes(EntityClasses& classes) {
    classes["func_door"] = make<FuncDoor>();
    classes["func_door_rotating"] = make<FuncDoorRotating>();
    classes["func_rotating"] = make<FuncRotating>();
    classes["func_areaportal"] = make<FuncAreaPortal>();
}

} // namespace openbl::sim
