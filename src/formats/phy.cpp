#include "openbl/formats/phy.hpp"

#include "binary_reader.hpp"

#include <string_view>
#include <unordered_map>

namespace openbl::formats {
namespace {

constexpr float kMetresToInches = 1.0f / 0.0254f;
constexpr std::size_t kSolidHeaderSize = 48;
constexpr std::size_t kTreeNodeSize = 28;
constexpr std::size_t kPieceHeaderSize = 16;
constexpr std::size_t kTriangleSize = 16;
constexpr std::size_t kPointSize = 16;
constexpr std::size_t kMaxNodes = 1 << 20;

std::array<float, 3> to_model(float x, float y, float z, float scale) {
    return {x * scale, z * scale, -y * scale};
}

} // namespace

PhyFile parse_phy(std::span<const std::byte> data) {
    detail::BinaryReader r(data, "PHY");
    PhyFile phy;
    const std::int32_t header_size = r.i32();
    if (header_size != 16) {
        r.fail("unexpected header size " + std::to_string(header_size));
    }
    r.i32(); // id, 0
    const std::int32_t solid_count = r.i32();
    phy.checksum = r.i32();
    if (solid_count < 0 || solid_count > 1024) {
        r.fail("implausible solid count");
    }

    std::size_t at = 16;
    for (std::int32_t s = 0; s < solid_count; ++s) {
        r.seek(at);
        const std::int32_t size = r.i32();
        const std::size_t solid = at + 4;
        if (size < static_cast<std::int32_t>(kSolidHeaderSize) || static_cast<std::size_t>(size) > data.size() - solid) {
            r.fail("solid size out of range");
        }
        const std::size_t end = solid + static_cast<std::size_t>(size);
        auto in_solid = [&](std::size_t pos, std::size_t bytes) {
            if (pos < solid || pos > end || bytes > end - pos) {
                r.fail("offset outside its solid");
            }
        };
        auto rel = [&](std::size_t base, std::int32_t offset) {
            const auto target = static_cast<std::int64_t>(base) + offset;
            if (target < static_cast<std::int64_t>(solid) || target > static_cast<std::int64_t>(end)) {
                r.fail("offset outside its solid");
            }
            return static_cast<std::size_t>(target);
        };

        PhySolid out;
        r.seek(solid);
        const float mx = r.f32(), my = r.f32(), mz = r.f32();
        out.mass_center = to_model(mx, my, mz, kMetresToInches);
        out.inertia = {r.f32(), r.f32(), r.f32()};
        out.radius = r.f32() * kMetresToInches;
        r.i32(); // Packed byte size and maximum deviation.
        const std::size_t tree = rel(solid, r.i32());
        // The tag at +44 is "IVPS", or zero in ragdoll solids.

        // Pieces are found through the bounding tree: 28-byte nodes of
        // {right child offset, piece offset, center[3], radius, packed box};
        // the left child follows its parent and leaves have no right child.
        std::vector<std::size_t> stack{tree};
        std::size_t visited = 0;
        while (!stack.empty()) {
            const std::size_t node = stack.back();
            stack.pop_back();
            if (++visited > kMaxNodes) {
                r.fail("physics tree too large");
            }
            in_solid(node, kTreeNodeSize);
            r.seek(node);
            const std::int32_t right = r.i32();
            const std::int32_t piece_offset = r.i32();
            if (right != 0) {
                stack.push_back(rel(node, right));
                stack.push_back(node + kTreeNodeSize);
                continue;
            }

            // Piece: {points offset, client data, flags, u16 triangle count, u16},
            // then 16-byte triangles {u32 index/flags, 3 x u32 edge}. Each edge's
            // low 16 bits are its start point; points are float[4] in metres.
            const std::size_t piece = rel(node, piece_offset);
            in_solid(piece, kPieceHeaderSize);
            r.seek(piece);
            const std::int32_t points_offset = r.i32();
            r.i32();
            r.u32();
            const std::uint16_t triangle_count = r.u16();
            in_solid(piece + kPieceHeaderSize, std::size_t{triangle_count} * kTriangleSize);
            const std::size_t points = rel(piece, points_offset);

            PhyConvex convex;
            std::unordered_map<std::uint16_t, std::uint32_t> remap;
            for (std::uint16_t t = 0; t < triangle_count; ++t) {
                std::array<std::uint32_t, 3> tri{};
                for (std::size_t e = 0; e < 3; ++e) {
                    r.seek(piece + kPieceHeaderSize + std::size_t{t} * kTriangleSize + 4 + 4 * e);
                    const std::uint16_t point = r.u16();
                    auto [it, inserted] = remap.try_emplace(point, static_cast<std::uint32_t>(convex.vertices.size()));
                    if (inserted) {
                        const std::size_t p = points + std::size_t{point} * kPointSize;
                        in_solid(p, 12);
                        r.seek(p);
                        const float x = r.f32(), y = r.f32(), z = r.f32();
                        convex.vertices.push_back(to_model(x, y, z, kMetresToInches));
                    }
                    tri[e] = it->second;
                }
                convex.triangles.push_back(tri);
            }
            out.pieces.push_back(std::move(convex));
        }
        phy.solids.push_back(std::move(out));
        at = end;
    }

    const auto* text = reinterpret_cast<const char*>(data.data() + at);
    std::string_view rest(text, data.size() - at);
    if (const auto nul = rest.find('\0'); nul != std::string_view::npos) {
        rest = rest.substr(0, nul);
    }
    phy.text = parse_keyvalues(rest);
    return phy;
}

} // namespace openbl::formats
