#pragma once

// The entity class registry's parts: each source file of classes adds its own.

#include "openbl/sim/world.hpp"

#include <memory>

namespace openbl::sim {

template <typename T>
EntityFactory make() {
    return [](EntityId id, formats::BspEntityPairs kv) -> std::unique_ptr<Entity> {
        return std::make_unique<T>(id, std::move(kv));
    };
}

void add_logic_classes(EntityClasses& classes);  // logic.cpp
void add_effect_classes(EntityClasses& classes); // effects.cpp
void add_mover_classes(EntityClasses& classes);  // movers.cpp
void add_trigger_classes(EntityClasses& classes); // triggers.cpp
void add_usable_classes(EntityClasses& classes);  // usables.cpp

} // namespace openbl::sim
