#pragma once

// The server's view of one map: its entities, its clock and the queue of
// inputs on their way (entity I/O). Single-player runs this as a local
// server; the client draws what it reports (hidden entities, switchable light
// styles) instead of reading the map directly.

#include "openbl/formats/bsp.hpp"
#include "openbl/sim/entity.hpp"
#include "openbl/sim/script_host.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::sim {

// Light styles 32..63 belong to switchable lights; the map compiler gives each
// named light (or group of lights with one name) its own.
constexpr std::size_t kLightStyleCount = 64;
constexpr std::size_t kFirstSwitchableStyle = 32;

struct WorldStats {
    std::size_t entities = 0;
    std::size_t outputs = 0;   // Connections, over all entities.
    std::size_t inputs = 0;    // Inputs delivered so far.
    std::size_t unhandled = 0; // Inputs no class or common input understood.
    std::size_t scripts = 0;   // Python statements handed to the script host.
    // Classes without their own behaviour yet, with how many entities each.
    std::map<std::string, std::size_t> generic_classes;
    // Inputs nobody handled, by "class.Input", for finding what to add next.
    std::map<std::string, std::size_t> unhandled_inputs;
};

// One delivered input or script statement, for tools that show the I/O as it runs.
struct TraceEntry {
    double time = 0;
    bool script = false;
    EntityId entity = kNoEntity; // Who received the input (kNoEntity: its target named nothing).
    std::string_view target;     // As the connection named it.
    std::string_view input;      // Or the statement.
    std::string_view parameter;
    EntityId caller = kNoEntity;
    bool handled = false;        // Some class or common input understood it.
};

class World {
public:
    // Creates an entity per map entity; `scripts` (may be null) runs the
    // Python statements outputs carry.
    explicit World(const formats::BspFile& bsp, ScriptHost* scripts = nullptr);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Spawns every entity, then activates them (logic_auto fires).
    void start();
    // Runs the clock `seconds` forward: queued inputs and thinks due by then,
    // in time order (inputs queued with no delay run in the same step).
    void advance(double seconds);
    double time() const { return time_; }

    Entity* entity(EntityId id);
    const Entity* entity(EntityId id) const;
    std::size_t entity_count() const { return entities_.size(); }
    // Entities whose targetname matches `name` (case-insensitive; a trailing
    // '*' matches any ending), else those of that classname.
    std::vector<EntityId> find(std::string_view name) const;

    // Queues `input` on every entity `target` names, after `delay` seconds.
    // Keywords: !self and !caller (the caller), !activator. (!player and
    // !playercontroller name nothing until the player exists.)
    void queue_input(std::string_view target, std::string_view input, std::string_view parameter, double delay,
                     EntityId activator, EntityId caller);
    // Queues a Python statement for the script host, after `delay` seconds.
    void queue_script(std::string_view statement, double delay, const ScriptContext& context);
    void remove(EntityId id);
    // Drops the queued inputs and statements an entity's outputs sent (logic_relay's CancelPending).
    void cancel_pending(EntityId caller);

    // Switchable light styles (32..63): on or off. Styles below 32 are not
    // switched here (0 is always on, 1..12 animate on their own).
    bool light_style_on(std::size_t style) const { return style < kLightStyleCount && style_on_[style]; }
    void set_light_style(std::size_t style, bool on);
    // Changes every time a switchable style or an entity's visibility changes.
    std::uint64_t revision() const { return revision_; }
    void touch() { ++revision_; }

    const WorldStats& stats() const { return stats_; }
    void set_trace(std::function<void(const TraceEntry&)> trace) { trace_ = std::move(trace); }

private:
    struct Event {
        double time;
        std::uint64_t order;  // Ties run in the order they were queued.
        bool script;          // A Python statement instead of an input.
        std::string target;   // Resolved when the event runs, as in Source.
        std::string input;    // Or the statement.
        std::string parameter;
        EntityId activator;
        EntityId caller;
    };
    struct Later {
        bool operator()(const Event& a, const Event& b) const {
            return a.time != b.time ? a.time > b.time : a.order > b.order;
        }
    };
    std::vector<EntityId> resolve(std::string_view target, EntityId activator, EntityId caller) const;
    void deliver(const Event& event);

    std::vector<std::unique_ptr<Entity>> entities_;
    std::priority_queue<Event, std::vector<Event>, Later> events_;
    std::uint64_t next_order_ = 0;
    double time_ = 0.0;
    ScriptHost* scripts_ = nullptr;
    std::array<bool, kLightStyleCount> style_on_{};
    std::uint64_t revision_ = 0;
    WorldStats stats_;
    std::function<void(const TraceEntry&)> trace_;
};

// Builds the entity for one map entity; the class registry maps classnames
// to these (classes without one become plain Entity objects).
using EntityFactory = std::function<std::unique_ptr<Entity>(EntityId, formats::BspEntityPairs)>;
using EntityClasses = std::map<std::string, EntityFactory, std::less<>>; // By lowercase classname.
const EntityClasses& entity_classes();

} // namespace openbl::sim
