#include "openbl/sim/world.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

World::World(const formats::BspFile& bsp, ScriptHost* scripts)
    : bsp_(bsp), collision_(std::make_unique<Collision>(bsp)), scripts_(scripts) {
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
    // Brush entities that block: everything with a brush model but the
    // volumes (triggers, illusions, particle and fog volumes, portals).
    for (const auto& e : entities_) {
        const std::string_view model = e->value("model");
        const std::string& cls = e->classname();
        if (model.size() < 2 || model[0] != '*' || cls == "worldspawn" || cls.rfind("trigger_", 0) == 0 ||
            cls == "func_illusionary" || cls == "func_areaportal" || cls == "func_areaportalwindow" ||
            cls == "func_particle" || cls == "func_dustmotes" || cls == "func_lod" || cls == "func_precipitation" ||
            cls == "func_ladder" || cls == "func_monitor" || cls == "func_occluder" ||
            (cls == "func_brush" && e->value("Solidity") == "1") ||
            (cls == "func_rotating" && (e->spawnflags() & 64))) {
            continue;
        }
        const auto index = static_cast<std::size_t>(std::strtoul(std::string(model.substr(1)).c_str(), nullptr, 10));
        if (index > 0 && index < bsp_.models.size()) {
            solid_brushes_.emplace_back(e->id(), index);
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

Trace World::trace(const formats::Vec3& start, const formats::Vec3& end, const formats::Vec3& mins,
                   const formats::Vec3& maxs, std::int32_t mask, EntityId ignore) const {
    Trace best = collision_->trace(0, start, end, mins, maxs, mask);
    for (const auto& [id, model] : solid_brushes_) {
        const Entity& e = *entities_[id];
        if (id == ignore || e.removed() || e.hidden()) {
            continue;
        }
        // Its model space to the world: the map pose, then any motion.
        Transform pose = Transform::translate(e.origin());
        if (const auto moved = render_transform(id)) {
            pose = moved->after(pose);
        }
        // Skip models nowhere near the sweep (a sphere around the model's bounds).
        const auto& m = bsp_.models[model];
        const formats::Vec3 centre = pose.apply({(m.mins[0] + m.maxs[0]) * 0.5f, (m.mins[1] + m.maxs[1]) * 0.5f,
                                                 (m.mins[2] + m.maxs[2]) * 0.5f});
        float radius = 0, sweep = 0;
        for (int k = 0; k < 3; ++k) {
            radius += (m.maxs[k] - m.mins[k]) * (m.maxs[k] - m.mins[k]) * 0.25f;
            sweep += (maxs[k] - mins[k]) * (maxs[k] - mins[k]) * 0.25f;
        }
        radius = std::sqrt(radius) + std::sqrt(sweep);
        auto distance_to_segment = [&] {
            formats::Vec3 d{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
            formats::Vec3 w{centre[0] - start[0], centre[1] - start[1], centre[2] - start[2]};
            const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            const float t = dd > 0 ? std::clamp((w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / dd, 0.0f, 1.0f) : 0.0f;
            const float x = w[0] - d[0] * t, y = w[1] - d[1] * t, z = w[2] - d[2] * t;
            return std::sqrt(x * x + y * y + z * z);
        };
        if (distance_to_segment() > radius) {
            continue;
        }
        Trace t = collision_->trace_moved(model, pose, start, end, mins, maxs, mask);
        if (t.all_solid || t.start_solid || t.fraction < best.fraction) {
            if (t.fraction < best.fraction || (t.start_solid && !best.start_solid)) {
                t.entity = static_cast<std::int32_t>(id);
                const bool start_solid = best.start_solid || t.start_solid;
                best = t;
                best.start_solid = start_solid;
            }
        }
    }
    return best;
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
