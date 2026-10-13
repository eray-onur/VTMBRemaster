#include "classes.hpp"

namespace openbl::sim {

const EntityClasses& entity_classes() {
    static const EntityClasses classes = [] {
        EntityClasses c;
        add_logic_classes(c);
        add_effect_classes(c);
        add_mover_classes(c);
        add_trigger_classes(c);
        return c;
    }();
    return classes;
}

} // namespace openbl::sim
