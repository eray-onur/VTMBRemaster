#include "openbl/sim/entity.hpp"

#include "openbl/sim/world.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace openbl::sim {

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

Entity::Entity(EntityId id, formats::BspEntityPairs keyvalues) : id_(id), keyvalues_(std::move(keyvalues)) {
    classname_ = std::string(value("classname"));
    targetname_ = std::string(value("targetname"));
    hidden_ = value("StartHidden") == "1";
    const std::string_view model = value("model");
    if (model.size() > 4 && iequals(model.substr(model.size() - 4), ".mdl")) {
        skin_ = static_cast<std::int32_t>(number("skin"));
    }
    for (const auto& [key, val] : keyvalues_) {
        if (key.size() > 2 && (key[0] == 'O' || key[0] == 'o') && (key[1] == 'n' || key[1] == 'N')) {
            if (auto output = formats::parse_entity_output(val)) {
                outputs_.push_back({key, std::move(*output)});
            }
        }
    }
}

std::string_view Entity::value(std::string_view key) const {
    for (auto it = keyvalues_.rbegin(); it != keyvalues_.rend(); ++it) {
        if (iequals(it->first, key)) {
            return it->second;
        }
    }
    return {};
}

float Entity::number(std::string_view key, float fallback) const {
    const std::string text(value(key));
    if (text.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    return end == text.c_str() ? fallback : v;
}

void Entity::spawn(World&) {}

void Entity::activate(World&) {}

bool Entity::input(World&, std::string_view, std::string_view, EntityId, EntityId) {
    return false;
}

void Entity::think(World&) {
    next_think_.reset();
}

bool Entity::common_input(World& world, std::string_view name, std::string_view parameter, EntityId activator) {
    if (iequals(name, "Kill") || iequals(name, "KillHierarchy")) {
        world.remove(id_);
        return true;
    }
    if (iequals(name, "ScriptHide") || iequals(name, "ScriptUnhide")) {
        const bool hide = iequals(name, "ScriptHide");
        if (hidden_ != hide) {
            hidden_ = hide;
            world.touch();
        }
        return true;
    }
    for (int n = 1; n <= 4; ++n) {
        if (iequals(name, "FireUser" + std::to_string(n))) {
            fire_output(world, "OnUser" + std::to_string(n), activator, parameter);
            return true;
        }
    }
    if (iequals(name, "Skin") && skin_ >= 0) {
        const auto skin = static_cast<std::int32_t>(std::strtol(std::string(parameter).c_str(), nullptr, 10));
        if (skin != skin_ && skin >= 0) {
            skin_ = skin;
            world.touch();
        }
        return true;
    }
    if (iequals(name, "AddOutput")) {
        // "key value" sets a keyvalue; "OnOutput target:input:param:delay:times"
        // adds a connection (Source's form, with ':' between the fields).
        const std::size_t space = parameter.find(' ');
        if (space == std::string_view::npos) {
            return true;
        }
        const std::string key(parameter.substr(0, space));
        std::string rest(parameter.substr(space + 1));
        if (key.size() > 2 && (key[0] == 'O' || key[0] == 'o') && rest.find(':') != std::string::npos) {
            std::replace(rest.begin(), rest.end(), ':', ',');
            if (auto output = formats::parse_entity_output(rest)) {
                outputs_.push_back({key, std::move(*output)});
            }
        } else {
            keyvalues_.emplace_back(key, rest);
        }
        return true;
    }
    return false;
}

void Entity::fire_output(World& world, std::string_view name, EntityId activator, std::string_view parameter) {
    for (Connection& c : outputs_) {
        if (!iequals(c.output, name) || c.target.times == 0) {
            continue;
        }
        if (c.target.times > 0) {
            --c.target.times;
        }
        if (!c.target.input.empty()) {
            const std::string_view param = c.target.parameter.empty() ? parameter : std::string_view(c.target.parameter);
            world.queue_input(c.target.target, c.target.input, param, c.target.delay, activator, id_);
        }
        if (!c.target.python.empty()) {
            world.queue_script(c.target.python, c.target.delay, {id_, activator});
        }
    }
    std::erase_if(outputs_, [](const Connection& c) { return c.target.times == 0; });
}

} // namespace openbl::sim
