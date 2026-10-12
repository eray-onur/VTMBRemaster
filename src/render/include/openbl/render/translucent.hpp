#pragma once

// One queue for everything translucent, drawn in the original's order
// (docs/RENDERING.md): back to front by BSP slot (formats::BspDrawOrder); in
// a slot, world surfaces first, then entities farthest first.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace openbl::render {

struct TranslucentItem {
    std::uint32_t slot = 0;   // BspDrawOrder slot; larger is nearer.
    bool world = false;       // A world surface: before the slot's entities.
    float distance = 0;       // From the eye; farther first within a slot.
    std::uint32_t source = 0; // Which renderer draws it (the caller's numbering).
    std::uint32_t index = 0;  // That renderer's item.
};

inline void sort_back_to_front(std::vector<TranslucentItem>& items) {
    std::stable_sort(items.begin(), items.end(), [](const TranslucentItem& a, const TranslucentItem& b) {
        if (a.slot != b.slot) {
            return a.slot < b.slot;
        }
        if (a.world != b.world) {
            return a.world;
        }
        return a.distance > b.distance;
    });
}

} // namespace openbl::render
