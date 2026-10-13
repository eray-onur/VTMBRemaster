// Entity classes that are pure logic: map-load events, relays, timers, and
// lights that scripts switch on and off.

#include "classes.hpp"
#include "openbl/sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <random>
#include <string>

namespace openbl::sim {

namespace {

// logic_auto: fires when the map starts. Spawnflag 1 removes it afterwards.
class LogicAuto final : public Entity {
public:
    using Entity::Entity;
    void activate(World& world) override {
        // A map loaded fresh: what the original fires on a new game. Loading a
        // save (OnLoadGame) and level transitions come with saves (Phase 3).
        for (const char* output : {"OnMapSpawn", "OnMapLoad", "OnNewGame"}) {
            fire_output(world, output, id());
        }
        if (spawnflags() & 1) {
            world.remove(id());
        }
    }
};

// logic_relay: Trigger fires OnTrigger, unless disabled. Spawnflag 1: only once.
class LogicRelay final : public Entity {
public:
    LogicRelay(EntityId id, formats::BspEntityPairs kv) : Entity(id, std::move(kv)), enabled_(number("StartDisabled") == 0) {}
    void spawn(World& world) override {
        fire_output(world, "OnSpawn", id());
    }
    bool input(World& world, std::string_view name, std::string_view, EntityId activator, EntityId) override {
        if (iequals(name, "Trigger")) {
            if (enabled_) {
                fire_output(world, "OnTrigger", activator);
                if (spawnflags() & 1) {
                    world.remove(id());
                }
            }
        } else if (iequals(name, "Enable")) {
            enabled_ = true;
        } else if (iequals(name, "Disable")) {
            enabled_ = false;
        } else if (iequals(name, "Toggle")) {
            enabled_ = !enabled_;
        } else if (iequals(name, "CancelPending")) {
            world.cancel_pending(id());
        } else {
            return false;
        }
        return true;
    }

private:
    bool enabled_;
};

// logic_timer: fires OnTimer every RefireTime seconds while enabled (or after
// a random time between LowerRandomBound and UpperRandomBound with
// UseRandomTime). Spawnflag 1 makes it an oscillator, alternating OnTimerHigh
// and OnTimerLow instead.
class LogicTimer final : public Entity {
public:
    LogicTimer(EntityId id, formats::BspEntityPairs kv)
        : Entity(id, std::move(kv)), enabled_(number("StartDisabled") == 0), refire_(number("RefireTime")),
          random_(number("UseRandomTime") != 0), lower_(number("LowerRandomBound")),
          upper_(number("UpperRandomBound")), rng_(id) {}
    void spawn(World& world) override {
        if (enabled_) {
            schedule(world);
        }
    }
    void think(World& world) override {
        fire(world);
        schedule(world);
    }
    bool input(World& world, std::string_view name, std::string_view parameter, EntityId, EntityId) override {
        if (iequals(name, "Enable") || (iequals(name, "Toggle") && !enabled_)) {
            if (!enabled_) {
                enabled_ = true;
                schedule(world);
            }
        } else if (iequals(name, "Disable") || iequals(name, "Toggle")) {
            enabled_ = false;
            set_next_think(std::nullopt);
        } else if (iequals(name, "FireTimer")) {
            fire(world);
        } else if (iequals(name, "RefireTime")) {
            refire_ = std::strtof(std::string(parameter).c_str(), nullptr);
            if (enabled_) {
                schedule(world);
            }
        } else if (iequals(name, "ResetTimer")) {
            if (enabled_) {
                schedule(world);
            }
        } else if (iequals(name, "LowerRandomBound")) {
            lower_ = std::strtof(std::string(parameter).c_str(), nullptr);
        } else if (iequals(name, "UpperRandomBound")) {
            upper_ = std::strtof(std::string(parameter).c_str(), nullptr);
        } else {
            return false;
        }
        return true;
    }

private:
    void schedule(World& world) {
        double wait = refire_;
        if (random_) {
            std::uniform_real_distribution<double> d(std::min(lower_, upper_), std::max(lower_, upper_));
            wait = d(rng_);
        }
        // A zero refire time would fire every step; the original's timers
        // think at least once per tick (0.1 s at its default tick rate).
        set_next_think(world.time() + std::max(wait, 0.1));
    }
    void fire(World& world) {
        if (spawnflags() & 1) {
            fire_output(world, high_ ? "OnTimerHigh" : "OnTimerLow", id());
            high_ = !high_;
        } else {
            fire_output(world, "OnTimer", id());
        }
    }

    bool enabled_;
    float refire_;
    bool random_;
    float lower_, upper_;
    bool high_ = true;
    std::mt19937 rng_; // Seeded per entity: runs repeat exactly.
};

// light, light_spot: TurnOn, TurnOff and Toggle switch the light style the map
// compiler gave it (32 and up; lights without one cannot be switched).
class SwitchableLight final : public Entity {
public:
    using Entity::Entity;
    bool input(World& world, std::string_view name, std::string_view, EntityId, EntityId) override {
        const auto style = static_cast<std::size_t>(number("style"));
        if (iequals(name, "TurnOn")) {
            world.set_light_style(style, true);
        } else if (iequals(name, "TurnOff")) {
            world.set_light_style(style, false);
        } else if (iequals(name, "Toggle")) {
            world.set_light_style(style, !world.light_style_on(style));
        } else {
            return false;
        }
        return true;
    }
};

} // namespace

void add_logic_classes(EntityClasses& classes) {
    classes["logic_auto"] = make<LogicAuto>();
    classes["logic_relay"] = make<LogicRelay>();
    classes["logic_timer"] = make<LogicTimer>();
    classes["light"] = make<SwitchableLight>();
    classes["light_spot"] = make<SwitchableLight>();
}

} // namespace openbl::sim
