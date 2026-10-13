// Entity classes that only change what is drawn.

#include "classes.hpp"

namespace openbl::sim {

namespace {

// env_sprite: drawn while on. Spawnflag 1 ("Start on") starts it on;
// StartHidden starts it off either way.
class EnvSprite final : public Entity {
public:
    EnvSprite(EntityId id, formats::BspEntityPairs kv) : Entity(id, std::move(kv)) {
        set_hidden(!(spawnflags() & 1) || value("StartHidden") == "1");
    }
    bool input(World& world, std::string_view name, std::string_view, EntityId, EntityId) override {
        bool hide = hidden();
        if (iequals(name, "ShowSprite") || iequals(name, "TurnOn")) {
            hide = false;
        } else if (iequals(name, "HideSprite") || iequals(name, "TurnOff")) {
            hide = true;
        } else if (iequals(name, "ToggleSprite") || iequals(name, "Toggle")) {
            hide = !hide;
        } else {
            return false;
        }
        if (hide != hidden()) {
            set_hidden(hide);
            world.touch();
        }
        return true;
    }
};

} // namespace

void add_effect_classes(EntityClasses& classes) {
    classes["env_sprite"] = make<EnvSprite>();
}

} // namespace openbl::sim
