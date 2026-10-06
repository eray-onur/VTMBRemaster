#pragma once

// Bloodlines optimized model meshes (.dx80.vtx / .dx7_2bone.vtx, version 107):
// triangle lists for each mesh of a .mdl, per level of detail. See docs/formats/models.md.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openbl::formats {

constexpr std::int32_t kVtxVersion = 107;

namespace vtx_flags {
// Strip group flag: vertex records are bare 16-bit vertex references.
constexpr std::uint8_t CompactVertices = 0x10;
} // namespace vtx_flags

struct VtxStripGroup {
    std::uint8_t flags = 0;
    // Mesh-local vertex index (into the .mdl mesh) for each group vertex.
    std::vector<std::uint16_t> vertex_refs;
    // Triangle list, indexing vertex_refs.
    std::vector<std::uint16_t> indices;
};

struct VtxMesh {
    std::uint16_t flags = 0;
    std::vector<VtxStripGroup> strip_groups;

    // Triangle list in mesh-local vertex indices.
    std::vector<std::uint32_t> triangles() const;
};

struct VtxLod {
    float switch_point = 0;
    std::vector<VtxMesh> meshes;
};

struct VtxModel {
    std::vector<VtxLod> lods;
};

struct VtxBodyPart {
    std::vector<VtxModel> models;
};

struct VtxFile {
    std::int32_t version = 0;
    std::int32_t checksum = 0; // Matches the .mdl checksum.
    std::int32_t lod_count = 0;
    std::vector<VtxBodyPart> body_parts;
};

// Throws FormatError.
VtxFile parse_vtx(std::span<const std::byte> data);

} // namespace openbl::formats
