#include "openbl/sim/world.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace openbl::sim {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Case-insensitive match, with a trailing '*' matching any ending.
bool name_matches(std::string_view pattern, std::string_view name) {
    if (!pattern.empty() && pattern.back() == '*') {
        pattern.remove_suffix(1);
        return name.size() >= pattern.size() && iequals(name.substr(0, pattern.size()), pattern);
    }
    return iequals(pattern, name);
}

// A safety net: a think that keeps rescheduling itself at the current time
// would otherwise never let the clock move.
constexpr std::size_t kMaxStepsPerAdvance = 1'000'000;

} // namespace

World::World(const formats::BspFile& bsp, ScriptHost* scripts) : bsp_(bsp), scripts_(scripts) {
    const auto& classes = entity_classes();
    entities_.reserve(bsp.entity_pairs.size());
    for (const formats::BspEntityPairs& pairs : bsp.entity_pairs) {
        const auto id = static_cast<EntityId>(entities_.size());
        std::string classname;
        for (const auto& [key, value] : pairs) {
            if (iequals(key, "classname")) {
                classname = lower(value);
            }
        }
        if (auto it = classes.find(classname); it != classes.end()) {
            entities_.push_back(it->second(id, pairs));
        } else {
            entities_.push_back(std::make_unique<Entity>(id, pairs));
            ++stats_.generic_classes[classname];
        }
        stats_.outputs += entities_.back()->output_count();
    }
    stats_.entities = entities_.size();

    // Switchable styles start on, unless every light using one starts dark
    // (spawnflag 1), as the lightmaps were compiled.
    std::array<bool, kLightStyleCount> seen{}, any_on{};
    for (const auto& e : entities_) {
        if (e->classname().rfind("light", 0) != 0 || e->value("style").empty()) {
            continue;
        }
        const auto style = static_cast<std::size_t>(e->number("style"));
        if (style >= kFirstSwitchableStyle && style < kLightStyleCount) {
            seen[style] = true;
            any_on[style] = any_on[style] || (e->spawnflags() & 1) == 0;
        }
    }
    for (std::size_t s = 0; s < kLightStyleCount; ++s) {
        style_on_[s] = !seen[s] || any_on[s];
    }
}

World::~World() = default;

void World::start() {
    // Parents by name (the first entity so named, as Source takes).
    for (const auto& e : entities_) {
        if (const std::string_view parent = e->value("parentname"); !parent.empty()) {
            const auto found = find(parent);
            if (!found.empty() && found.front() != e->id()) {
                e->parent_ = found.front();
            }
        }
    }
    for (const auto& e : entities_) {
        e->spawn(*this);
    }
    for (const auto& e : entities_) {
        if (!e->removed()) {
            e->activate(*this);
        }
    }
    touch();
}

void World::advance(double seconds) {
    const double end = time_ + std::max(seconds, 0.0);
    for (std::size_t step = 0; step < kMaxStepsPerAdvance; ++step) {
        double think_time = std::numeric_limits<double>::infinity();
        Entity* thinker = nullptr;
        for (const auto& e : entities_) {
            if (!e->removed() && e->next_think() && *e->next_think() < think_time) {
                think_time = *e->next_think();
                thinker = e.get();
            }
        }
        const double event_time = events_.empty() ? std::numeric_limits<double>::infinity() : events_.top().time;
        if (std::min(event_time, think_time) > end) {
            break;
        }
        if (event_time <= think_time) {
            const Event event = events_.top();
            events_.pop();
            time_ = std::max(time_, event.time);
            deliver(event);
        } else {
            time_ = std::max(time_, think_time);
            thinker->set_next_think(std::nullopt);
            thinker->think(*this);
        }
    }
    time_ = end;
}

Entity* World::entity(EntityId id) {
    return id < entities_.size() ? entities_[id].get() : nullptr;
}

const Entity* World::entity(EntityId id) const {
    return id < entities_.size() ? entities_[id].get() : nullptr;
}

std::vector<EntityId> World::find(std::string_view name) const {
    std::vector<EntityId> out;
    if (name.empty()) {
        return out;
    }
    for (const auto& e : entities_) {
        if (!e->removed() && !e->targetname().empty() && name_matches(name, e->targetname())) {
            out.push_back(e->id());
        }
    }
    if (out.empty()) {
        for (const auto& e : entities_) {
            if (!e->removed() && name_matches(name, e->classname())) {
                out.push_back(e->id());
            }
        }
    }
    return out;
}

std::vector<EntityId> World::resolve(std::string_view target, EntityId activator, EntityId caller) const {
    auto alive = [&](EntityId id) {
        const Entity* e = entity(id);
        return e && !e->removed() ? std::vector<EntityId>{id} : std::vector<EntityId>{};
    };
    if (iequals(target, "!self") || iequals(target, "!caller")) {
        return alive(caller);
    }
    if (iequals(target, "!activator")) {
        return alive(activator);
    }
    if (!target.empty() && target.front() == '!') {
        return {}; // !player, !playercontroller: no player yet.
    }
    return find(target);
}

void World::queue_input(std::string_view target, std::string_view input, std::string_view parameter, double delay,
                        EntityId activator, EntityId caller) {
    events_.push({time_ + std::max(delay, 0.0), next_order_++, false, std::string(target), std::string(input),
                  std::string(parameter), activator, caller});
}

void World::queue_script(std::string_view statement, double delay, const ScriptContext& context) {
    events_.push({time_ + std::max(delay, 0.0), next_order_++, true, {}, std::string(statement), {},
                  context.activator, context.caller});
}

void World::cancel_pending(EntityId caller) {
    std::vector<Event> kept;
    while (!events_.empty()) {
        if (events_.top().caller != caller) {
            kept.push_back(events_.top());
        }
        events_.pop();
    }
    for (Event& e : kept) {
        events_.push(std::move(e));
    }
}

void World::deliver(const Event& event) {
    if (event.script) {
        ++stats_.scripts;
        if (trace_) {
            trace_({time_, true, kNoEntity, {}, event.input, {}, event.caller, scripts_ != nullptr});
        }
        if (scripts_) {
            scripts_->run(event.input, {event.caller, event.activator});
        }
        return;
    }
    const auto targets = resolve(event.target, event.activator, event.caller);
    if (targets.empty() && trace_) {
        trace_({time_, false, kNoEntity, event.target, event.input, event.parameter, event.caller, false});
    }
    for (EntityId id : targets) {
        Entity& e = *entities_[id];
        if (e.removed()) {
            continue;
        }
        ++stats_.inputs;
        const bool handled = e.input(*this, event.input, event.parameter, event.activator, event.caller) ||
                             e.common_input(*this, event.input, event.parameter, event.activator);
        if (!handled) {
            ++stats_.unhandled;
            ++stats_.unhandled_inputs[lower(e.classname()) + "." + event.input];
        }
        if (trace_) {
            trace_({time_, false, id, event.target, event.input, event.parameter, event.caller, handled});
        }
    }
}

void World::remove(EntityId id) {
    if (Entity* e = entity(id); e && !e->removed_) {
        e->removed_ = true;
        e->next_think_.reset();
        touch();
    }
}

std::optional<Transform> World::render_transform(EntityId id) const {
    std::optional<Transform> out;
    // Walk up the parents (bounded, in case a map names a cycle).
    for (int depth = 0; depth < 16 && id != kNoEntity; ++depth) {
        const Entity* e = entity(id);
        if (!e) {
            break;
        }
        if (e->motion()) {
            out = out ? e->motion()->after(*out) : *e->motion();
        }
        id = e->parent();
    }
    return out;
}

bool World::portal_open(int portal) const {
    const auto it = portals_.find(portal);
    return it == portals_.end() || it->second;
}

void World::set_portal_open(int portal, bool open) {
    auto [it, inserted] = portals_.try_emplace(portal, open);
    if (inserted || it->second != open) {
        it->second = open;
        touch();
    }
}

void World::set_light_style(std::size_t style, bool on) {
    if (style >= kFirstSwitchableStyle && style < kLightStyleCount && style_on_[style] != on) {
        style_on_[style] = on;
        touch();
    }
}

} // namespace openbl::sim
