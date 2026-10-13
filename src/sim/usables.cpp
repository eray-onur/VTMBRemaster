// Things the player uses: door knobs, buttons and switches.

#include "classes.hpp"

#include <algorithm>

namespace openbl::sim {

namespace {

// Small props used from close by (the use ray passes within this of them).
constexpr float kPropUseRadius = 12.0f;

// prop_doorknob, prop_doorknob_electronic: using one uses the door it is
// parented to. (Lockpicking, keys and hacking come with the game rules.)
class DoorKnob final : public Entity {
public:
    using Entity::Entity;
    float use_radius() const override { return kPropUseRadius; }
    bool use(World& world, EntityId activator) override {
        fire_output(world, "OnUseBegin", activator);
        Entity* door = world.entity(parent());
        const bool used = door && !door->removed() && door->use(world, activator);
        fire_output(world, "OnUseEnd", activator);
        return used;
    }
};

// func_button: OnPressed and OnIn when used, OnOut when it comes back after
// "wait" seconds (-1: it stays in, and cannot be pressed again). Spawnflag
// 2048 starts it locked: then it fires OnUseLocked. Lock and Unlock.
class FuncButton final : public Entity {
public:
    FuncButton(EntityId id, formats::BspEntityPairs kv) : Entity(id, std::move(kv)) {
        locked_ = (spawnflags() & 2048) != 0;
    }
    bool use(World& world, EntityId activator) override {
        if (locked_) {
            fire_output(world, "OnUseLocked", activator);
            return true;
        }
        if (in_) {
            return true;
        }
        in_ = true;
        activator_ = activator;
        fire_output(world, "OnPressed", activator);
        fire_output(world, "OnIn", activator);
        if (const float wait = number("wait", 1.0f); wait >= 0) {
            set_next_think(world.time() + std::max(wait, 0.1f));
        }
        return true;
    }
    void think(World& world) override {
        in_ = false;
        fire_output(world, "OnOut", activator_);
    }
    bool input(World& world, std::string_view name, std::string_view, EntityId activator, EntityId) override {
        if (iequals(name, "Lock")) {
            locked_ = true;
        } else if (iequals(name, "Unlock")) {
            locked_ = false;
        } else if (iequals(name, "Press")) {
            use(world, activator);
        } else {
            return false;
        }
        return true;
    }

private:
    bool locked_;
    bool in_ = false;
    EntityId activator_ = kNoEntity;
};

// prop_button: OnPressed (OnPressedLocked when locked); prop_switch: OnUse.
class PropButton final : public Entity {
public:
    PropButton(EntityId id, formats::BspEntityPairs kv) : Entity(id, std::move(kv)) {
        locked_ = (spawnflags() & 2048) != 0;
    }
    float use_radius() const override { return kPropUseRadius; }
    bool use(World& world, EntityId activator) override {
        if (classname() == "prop_switch") {
            fire_output(world, "OnUse", activator);
        } else {
            fire_output(world, locked_ ? "OnPressedLocked" : "OnPressed", activator);
        }
        return true;
    }
    bool input(World&, std::string_view name, std::string_view, EntityId, EntityId) override {
        if (iequals(name, "Lock") || iequals(name, "Unlock")) {
            locked_ = iequals(name, "Lock");
            return true;
        }
        return false;
    }

private:
    bool locked_;
};

} // namespace

void add_usable_classes(EntityClasses& classes) {
    classes["prop_doorknob"] = make<DoorKnob>();
    classes["prop_doorknob_electronic"] = make<DoorKnob>();
    classes["func_button"] = make<FuncButton>();
    classes["prop_button"] = make<PropButton>();
    classes["prop_switch"] = make<PropButton>();
}

} // namespace openbl::sim
