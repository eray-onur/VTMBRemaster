#pragma once

// AI navigation graphs (maps/graphs/<map>.ain, text, version 30) and their
// companion .loc files. See docs/formats/ai_graphs.md.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openbl::formats {

struct AinNode {
    std::array<float, 3> origin{};
    float yaw = 0;                    // Degrees.
    std::vector<float> hull_offsets;  // One per hull (22); vertical offset for that hull.
    std::int32_t type = 0;            // 2 for almost every node, 1 for a few.
    std::int32_t info = 0;            // 0 in every shipped graph.
    std::vector<std::uint32_t> mask;  // One bit per node in the graph; meaning unverified (never has its own bit).
    std::int32_t zone = 0;            // Index into AinGraph::zones.
    std::int32_t link_count = 0;      // Links that start or end at this node.

    bool mask_bit(std::size_t node) const {
        return node / 32 < mask.size() && ((mask[node / 32] >> (node % 32)) & 1u);
    }
};

struct AinLink {
    std::int32_t source = 0, dest = 0; // Node indices.
    std::int32_t flags = 0;            // 0, or 8192 on a few links.
    std::vector<std::int32_t> hull_moves; // One per hull: 0 = not traversable; 1 and 2 are move types (unverified).
};

struct AinGraph {
    std::int32_t version = 0; // 30.
    std::int32_t used_hull_bits = 0;
    std::vector<std::int32_t> zones; // ZoneCount values; meaning unverified.
    std::vector<AinNode> nodes;
    std::vector<AinLink> links;
    std::vector<std::int32_t> editor_ids; // WCLookup: the editor's id for each node.
};

// Throws FormatError on malformed input.
AinGraph parse_ain(std::string_view text);

// The single number in a .loc file. Meaning unknown: not the map revision
// and not a CRC32 of the .bsp.
std::int64_t parse_loc(std::string_view text);

} // namespace openbl::formats
