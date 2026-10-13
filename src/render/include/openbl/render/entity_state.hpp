#pragma once

// What the client draws of one map entity, as the simulation reports it
// (docs/ARCHITECTURE.md, decision 1). Indexed like BspFile::entities.

#include <cstdint>

namespace openbl::render {

struct EntityState {
    bool hidden = false;    // StartHidden, ScriptHide, Kill, sprites switched off...
    std::int32_t skin = -1; // Models: the skin to draw; -1 keeps the map's.
};

} // namespace openbl::render
