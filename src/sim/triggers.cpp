// Touch triggers: brush volumes that fire outputs when the player enters,
// stays in and leaves them.

#include "classes.hpp"
#include "openbl/sim/player.hpp"
#include "openbl/sim/triggers.hpp"

#include <cstdlib>
#include <string>

namespace openbl::sim {

Trigger::Trigger(EntityId id, formats::BspEntityPairs kv) : Entity(id, std::move(kv)) {
    enabled_ = number("StartDisabled") == 0;
}

bool Trigger::active() const {
    // Spawnflag 1: clients (the player) touch it. Filters (filter_activator_*)
    // are not read yet; the shipped ones mostly pick out NPCs, so a filtered
    // trigger waits for them.
    return enabled_ && !hidden() && !removed() && (spawnflags() & 1) && value("filtername").empty();
}

bool Trigger::input(World&, std::string_view name, std::string_view, EntityId, EntityId) {
    if (iequals(name, "Enable")) {
        enabled_ = true;
    } else if (iequals(name, "Disable")) {
        enabled_ = false;
    } else if (iequals(name, "Toggle")) {
        enabled_ = !enabled_;
    } else {
        return false;
    }
    return true;
}

void Trigger::start_touch(World& world, EntityId activator) {
    fire_output(world, "OnStartTouch", activator);
}

void Trigger::touching(World&, EntityId) {}

void Trigger::end_touch(World& world, EntityId activator) {
    fire_output(world, "OnEndTouch", activator);
}

namespace {

// trigger_multiple: OnTrigger on entering and again every "wait" seconds
// while touched.
class TriggerMultiple final : public Trigger {
public:
    using Trigger::Trigger;
    void start_touch(World& world, EntityId activator) override {
        Trigger::start_touch(world, activator);
        touching(world, activator);
    }
    void touching(World& world, EntityId activator) override {
        if (world.time() < next_trigger_) {
            return;
        }
        fire_output(world, "OnTrigger", activator);
        next_trigger_ = world.time() + std::max(number("wait", 1.0f), 0.0f);
    }

private:
    double next_trigger_ = 0;
};

// trigger_once: OnStartTouch and OnTrigger on entering, then it is gone.
class TriggerOnce final : public Trigger {
public:
    using Trigger::Trigger;
    void start_touch(World& world, EntityId activator) override {
        Trigger::start_touch(world, activator);
        fire_output(world, "OnTrigger", activator);
        world.remove(id());
    }
};

// trigger_teleport: moves the player to its target's origin (an
// info_teleport_destination). The landmark offset and the destination's
// angles wait for the player's view to live on the server.
class TriggerTeleport final : public Trigger {
public:
    using Trigger::Trigger;
    void start_touch(World& world, EntityId activator) override {
        Trigger::start_touch(world, activator);
        const auto targets = world.find(value("target"));
        Player* player = world.player();
        if (!targets.empty() && player && activator == player->id()) {
            player->teleport(world, world.entity(targets.front())->origin());
        }
    }
};

} // namespace

void add_trigger_classes(EntityClasses& classes) {
    classes["trigger_multiple"] = make<TriggerMultiple>();
    classes["trigger_once"] = make<TriggerOnce>();
    classes["trigger_teleport"] = make<TriggerTeleport>();
}

} // namespace openbl::sim
