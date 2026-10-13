#pragma once

// Server-side entities (docs/ARCHITECTURE.md, decision 1): created from the
// map's entity lump, they receive inputs, fire outputs and think over time.
// Classes without their own behaviour yet are plain Entity objects: they keep
// their keyvalues, take the inputs every entity understands and fire their
// outputs when told to (FireUser).

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/entity_io.hpp"
#include "openbl/sim/script_host.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::sim {

class World;

class Entity {
public:
    Entity(EntityId id, formats::BspEntityPairs keyvalues);
    virtual ~Entity() = default;
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    EntityId id() const { return id_; }
    const std::string& classname() const { return classname_; }
    const std::string& targetname() const { return targetname_; }
    // The last value of `key` (case-insensitive), or empty.
    std::string_view value(std::string_view key) const;
    float number(std::string_view key, float fallback = 0.0f) const;
    int spawnflags() const { return static_cast<int>(number("spawnflags")); }
    const formats::BspEntityPairs& keyvalues() const { return keyvalues_; }

    // Hidden entities are not drawn ("StartHidden", ScriptHide / ScriptUnhide).
    bool hidden() const { return hidden_; }
    // Killed entities stay in the world's table until the end of the tick.
    bool removed() const { return removed_; }

    // After every entity of the map exists, in map order.
    virtual void spawn(World& world);
    // Once the map has spawned and is running (logic_auto fires here).
    virtual void activate(World& world);
    // Handles an input of this class; false if the class has no such input
    // (the world then tries the inputs every entity has).
    virtual bool input(World& world, std::string_view name, std::string_view parameter, EntityId activator,
                       EntityId caller);
    // Called when the world's clock reaches next_think().
    virtual void think(World& world);
    std::optional<double> next_think() const { return next_think_; }

    // The inputs every entity has: Kill, ScriptHide, ScriptUnhide,
    // FireUser1..4 (fire OnUser1..4) and AddOutput.
    bool common_input(World& world, std::string_view name, std::string_view parameter, EntityId activator);

    // Fires every connection of output `name`: their inputs are queued on
    // their targets and their Python statements for the script host, each
    // after its delay. Connections that have used up their times are dropped.
    void fire_output(World& world, std::string_view name, EntityId activator, std::string_view parameter = {});
    std::size_t output_count() const { return outputs_.size(); }

protected:
    void set_next_think(std::optional<double> time) { next_think_ = time; }
    void set_hidden(bool hidden) { hidden_ = hidden; }

private:
    friend class World;
    struct Connection {
        std::string output; // e.g. "OnTrigger".
        formats::EntityOutput target;
    };

    EntityId id_;
    formats::BspEntityPairs keyvalues_;
    std::string classname_;
    std::string targetname_;
    std::vector<Connection> outputs_;
    std::optional<double> next_think_;
    bool hidden_ = false;
    bool removed_ = false;
};

// Case-insensitive comparison, as entity names, classes and inputs use.
bool iequals(std::string_view a, std::string_view b);

} // namespace openbl::sim
