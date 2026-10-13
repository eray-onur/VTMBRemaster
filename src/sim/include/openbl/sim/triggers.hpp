#pragma once

// Touch triggers: a brush volume the world checks the player against after
// each move (World::update_touches). Enable, Disable, Toggle; StartDisabled;
// hidden triggers do not fire.

#include "openbl/sim/entity.hpp"

namespace openbl::sim {

class Trigger : public Entity {
public:
    Trigger(EntityId id, formats::BspEntityPairs kv);

    // Whether the player can touch it now.
    bool active() const;
    bool input(World& world, std::string_view name, std::string_view parameter, EntityId activator,
               EntityId caller) override;

    // The player entered, is still inside (each check), left.
    virtual void start_touch(World& world, EntityId activator);
    virtual void touching(World& world, EntityId activator);
    virtual void end_touch(World& world, EntityId activator);

private:
    bool enabled_ = true;
};

} // namespace openbl::sim
