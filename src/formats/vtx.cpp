#include "openbl/formats/vtx.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"

namespace openbl::formats {

namespace {

constexpr std::size_t kBodyPartSize = 8;
constexpr std::size_t kModelSize = 8;
constexpr std::size_t kLodSize = 12;
constexpr std::size_t kMeshSize = 8;
constexpr std::size_t kStripGroupSize = 20;
constexpr std::size_t kStripSize = 16;
constexpr std::size_t kFullVertexSize = 12; // Bone palette data, then the 16-bit vertex reference.
constexpr std::uint32_t kMaxCount = 1 << 20;

class Parser {
public:
    explicit Parser(std::span<const std::byte> data) : data_(data), in_(data, "VTX") {}

    VtxFile run() {
        VtxFile vtx;
        vtx.version = i32(0);
        if (vtx.version != kVtxVersion) {
            in_.fail("unsupported version " + std::to_string(vtx.version));
        }
        vtx.checksum = i32(0x10);
        vtx.lod_count = i32(0x14);
        const std::uint32_t parts = count(0x1c);
        const std::size_t base = rel(0, i32(0x20));
        for (std::uint32_t b = 0; b < parts; ++b) {
            const std::size_t at = base + b * kBodyPartSize;
            VtxBodyPart part;
            const std::uint32_t models = count(at);
            const std::size_t model_base = rel(at, i32(at + 4));
            for (std::uint32_t m = 0; m < models; ++m) {
                part.models.push_back(parse_model(model_base + m * kModelSize));
            }
            vtx.body_parts.push_back(std::move(part));
        }
        return vtx;
    }

private:
    std::int32_t i32(std::size_t at) {
        in_.seek(at);
        return in_.i32();
    }
    std::uint16_t u16(std::size_t at) {
        in_.seek(at);
        return in_.u16();
    }
    std::uint8_t u8(std::size_t at) {
        in_.seek(at);
        return in_.u8();
    }
    std::uint32_t count(std::size_t at) {
        const std::int32_t n = i32(at);
        if (n < 0 || static_cast<std::uint32_t>(n) > kMaxCount) {
            in_.fail("implausible count " + std::to_string(n) + " at offset " + std::to_string(at));
        }
        return static_cast<std::uint32_t>(n);
    }
    std::size_t rel(std::size_t base, std::int32_t offset) {
        const auto target = static_cast<std::int64_t>(base) + offset;
        if (target < 0 || static_cast<std::uint64_t>(target) > data_.size()) {
            in_.fail("offset out of range");
        }
        return static_cast<std::size_t>(target);
    }

    VtxModel parse_model(std::size_t at) {
        VtxModel model;
        const std::uint32_t lods = count(at);
        const std::size_t lod_base = rel(at, i32(at + 4));
        for (std::uint32_t l = 0; l < lods; ++l) {
            const std::size_t lo = lod_base + l * kLodSize;
            VtxLod lod;
            const std::uint32_t meshes = count(lo);
            const std::size_t mesh_base = rel(lo, i32(lo + 4));
            in_.seek(lo + 8);
            lod.switch_point = in_.f32();
            for (std::uint32_t m = 0; m < meshes; ++m) {
                lod.meshes.push_back(parse_mesh(mesh_base + m * kMeshSize));
            }
            model.lods.push_back(std::move(lod));
        }
        return model;
    }

    VtxMesh parse_mesh(std::size_t at) {
        VtxMesh mesh;
        const std::uint16_t groups = u16(at);
        mesh.flags = u16(at + 2);
        const std::size_t group_base = rel(at, i32(at + 4));
        for (std::uint16_t g = 0; g < groups; ++g) {
            mesh.strip_groups.push_back(parse_strip_group(group_base + std::size_t{g} * kStripGroupSize));
        }
        return mesh;
    }

    VtxStripGroup parse_strip_group(std::size_t at) {
        VtxStripGroup group;
        const std::uint16_t vertex_count = u16(at);
        const std::uint16_t index_count = u16(at + 2);
        const std::uint16_t strip_count = u16(at + 4);
        group.flags = u8(at + 6);
        const std::size_t vertex_base = rel(at, i32(at + 8));
        const std::size_t index_base = rel(at, i32(at + 12));
        const std::size_t strip_base = rel(at, i32(at + 16));

        const std::size_t record = (group.flags & vtx_flags::CompactVertices) ? 2 : kFullVertexSize;
        group.vertex_refs.resize(vertex_count);
        for (std::size_t v = 0; v < vertex_count; ++v) {
            group.vertex_refs[v] = u16(vertex_base + v * record + record - 2);
        }
        if (index_count % 3 != 0) {
            in_.fail("strip group index count is not a multiple of 3");
        }
        group.indices.resize(index_count);
        for (std::size_t i = 0; i < index_count; ++i) {
            group.indices[i] = u16(index_base + 2 * i);
            if (group.indices[i] >= vertex_count) {
                in_.fail("strip group index out of range");
            }
        }

        // Strips partition the group's indices and vertices contiguously.
        std::uint32_t indices_seen = 0;
        std::uint32_t vertices_seen = 0;
        for (std::uint16_t s = 0; s < strip_count; ++s) {
            const std::size_t st = strip_base + std::size_t{s} * kStripSize;
            if (u16(st + 2) != indices_seen || u16(st + 6) != vertices_seen) {
                in_.fail("strips are not contiguous");
            }
            indices_seen += u16(st);
            vertices_seen += u16(st + 4);
        }
        if (strip_count > 0 && (indices_seen != index_count || vertices_seen != vertex_count)) {
            in_.fail("strip totals do not match their group");
        }
        return group;
    }

    std::span<const std::byte> data_;
    detail::BinaryReader in_;
};

} // namespace

std::vector<std::uint32_t> VtxMesh::triangles() const {
    std::vector<std::uint32_t> out;
    for (const VtxStripGroup& group : strip_groups) {
        for (std::uint16_t index : group.indices) {
            out.push_back(group.vertex_refs[index]);
        }
    }
    return out;
}

VtxFile parse_vtx(std::span<const std::byte> data) {
    return Parser(data).run();
}

} // namespace openbl::formats
