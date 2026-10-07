#include "openbl/formats/bsp.hpp"

#include "binary_reader.hpp"
#include "physics_solid.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::formats {

namespace {

constexpr std::size_t kHeaderSize = 8 + 16 * bsp_lump::Count + 4;
constexpr std::size_t kFaceSize = 104;
constexpr std::size_t kTexInfoSize = 72;
constexpr std::size_t kTexDataSize = 32;
constexpr std::size_t kModelSize = 48;
constexpr std::size_t kDispInfoSize = 176;
constexpr std::size_t kDispVertSize = 20;
constexpr std::size_t kStaticPropSize = 56;
constexpr std::size_t kStaticPropNameSize = 128;

struct Lump {
    std::size_t offset = 0;
    std::size_t length = 0;
};

class Parser {
public:
    explicit Parser(std::span<const std::byte> data) : data_(data), in_(data, "BSP") {}

    BspFile run() {
        BspFile bsp;
        if (data_.size() < kHeaderSize) {
            in_.fail("file too small for header");
        }
        const auto magic = in_.bytes(4);
        if (magic[0] != std::byte{'V'} || magic[1] != std::byte{'B'} || magic[2] != std::byte{'S'} ||
            magic[3] != std::byte{'P'}) {
            in_.fail("missing VBSP signature");
        }
        bsp.version = in_.i32();
        if (bsp.version != kBspVersion) {
            in_.fail("unsupported version " + std::to_string(bsp.version));
        }
        for (Lump& lump : lumps_) {
            const std::int32_t offset = in_.i32();
            const std::int32_t length = in_.i32();
            in_.bytes(8); // version, fourCC
            if (offset < 0 || length < 0 || static_cast<std::uint64_t>(offset) + length > data_.size()) {
                in_.fail("lump out of range");
            }
            lump = {static_cast<std::size_t>(offset), static_cast<std::size_t>(length)};
        }
        bsp.map_revision = in_.i32();

        const auto entity_bytes = lump_data(bsp_lump::Entities);
        std::string_view entity_text(reinterpret_cast<const char*>(entity_bytes.data()), entity_bytes.size());
        if (auto nul = entity_text.find('\0'); nul != std::string_view::npos) {
            entity_text = entity_text.substr(0, nul);
        }
        bsp.entities = parse_bsp_entities(entity_text);

        for_each(bsp_lump::Planes, 20, [&](detail::BinaryReader& r) {
            BspPlane p;
            p.normal = vec3(r);
            p.dist = r.f32();
            p.type = r.i32();
            bsp.planes.push_back(p);
        });
        for_each(bsp_lump::Vertices, 12, [&](detail::BinaryReader& r) { bsp.vertices.push_back(vec3(r)); });
        for_each(bsp_lump::Edges, 4, [&](detail::BinaryReader& r) {
            const std::uint16_t a = r.u16();
            bsp.edges.push_back({a, r.u16()});
        });
        for_each(bsp_lump::SurfEdges, 4, [&](detail::BinaryReader& r) { bsp.surf_edges.push_back(r.i32()); });
        parse_texdata(bsp);
        for_each(bsp_lump::TexInfo, kTexInfoSize, [&](detail::BinaryReader& r) {
            BspTexInfo t;
            for (auto& row : t.texture_vecs) {
                for (float& f : row) {
                    f = r.f32();
                }
            }
            for (auto& row : t.lightmap_vecs) {
                for (float& f : row) {
                    f = r.f32();
                }
            }
            t.flags = r.i32();
            t.texdata = r.i32();
            bsp.texinfos.push_back(t);
        });
        parse_faces(bsp);
        for_each(bsp_lump::Models, kModelSize, [&](detail::BinaryReader& r) {
            BspModel m;
            m.mins = vec3(r);
            m.maxs = vec3(r);
            m.origin = vec3(r);
            m.head_node = r.i32();
            m.first_face = r.i32();
            m.num_faces = r.i32();
            if (m.first_face < 0 || m.num_faces < 0 ||
                static_cast<std::size_t>(m.first_face) + static_cast<std::size_t>(m.num_faces) > bsp.faces.size()) {
                in_.fail("model faces out of range");
            }
            bsp.models.push_back(m);
        });
        const auto light = lump_data(bsp_lump::Lighting);
        bsp.lighting.assign(light.begin(), light.end());
        parse_displacements(bsp);
        parse_game_lump(bsp);
        for_each(bsp_lump::WorldLights, 88, [&](detail::BinaryReader& r) {
            BspWorldLight l;
            l.origin = vec3(r);
            l.intensity = vec3(r);
            l.normal = vec3(r);
            l.cluster = r.i32();
            l.type = static_cast<BspLightType>(r.i32());
            l.style = r.i32();
            l.stopdot = r.f32();
            l.stopdot2 = r.f32();
            l.exponent = r.f32();
            l.radius = r.f32();
            l.constant_attn = r.f32();
            l.linear_attn = r.f32();
            l.quadratic_attn = r.f32();
            l.flags = r.i32();
            l.texinfo = r.i32();
            l.owner = r.i32();
            bsp.world_lights.push_back(l);
        });
        for_each(bsp_lump::LeafFaces, 2, [&](detail::BinaryReader& r) {
            const std::uint16_t f = r.u16();
            if (f >= bsp.faces.size()) {
                in_.fail("leaf face out of range");
            }
            bsp.leaf_faces.push_back(f);
        });
        parse_brushes(bsp);
        for_each(bsp_lump::LeafWaterData, 12, [&](detail::BinaryReader& r) {
            BspLeafWater w;
            w.surface_z = r.f32();
            w.min_z = r.f32();
            w.texinfo = static_cast<std::int16_t>(r.u16());
            bsp.leaf_water.push_back(w);
        });
        parse_areas(bsp);
        for_each(bsp_lump::Leafs, 32, [&](detail::BinaryReader& r) {
            BspLeaf leaf;
            leaf.contents = r.i32();
            leaf.cluster = static_cast<std::int16_t>(r.u16());
            const std::uint16_t area_flags = r.u16();
            leaf.area = area_flags & 0x1ff;
            leaf.flags = static_cast<std::uint16_t>(area_flags >> 9);
            for (auto& v : leaf.mins) {
                v = static_cast<std::int16_t>(r.u16());
            }
            for (auto& v : leaf.maxs) {
                v = static_cast<std::int16_t>(r.u16());
            }
            leaf.first_leaf_face = r.u16();
            leaf.num_leaf_faces = r.u16();
            leaf.first_leaf_brush = r.u16();
            leaf.num_leaf_brushes = r.u16();
            leaf.water_data = static_cast<std::int16_t>(r.u16());
            if (std::size_t{leaf.first_leaf_face} + leaf.num_leaf_faces > bsp.leaf_faces.size() ||
                std::size_t{leaf.first_leaf_brush} + leaf.num_leaf_brushes > bsp.leaf_brushes.size() ||
                (leaf.water_data >= 0 && static_cast<std::size_t>(leaf.water_data) >= bsp.leaf_water.size()) ||
                (!bsp.areas.empty() && leaf.area >= bsp.areas.size())) {
                in_.fail("leaf references out of range");
            }
            bsp.leafs.push_back(leaf);
        });
        parse_visibility(bsp);
        for_each(bsp_lump::Cubemaps, 16, [&](detail::BinaryReader& r) {
            BspCubemap c;
            for (auto& v : c.origin) {
                v = r.i32();
            }
            c.size = r.i32();
            bsp.cubemaps.push_back(c);
        });
        parse_collision(bsp);
        for_each(bsp_lump::VertexNormals, 12, [&](detail::BinaryReader& r) { bsp.vertex_normals.push_back(vec3(r)); });
        for_each(bsp_lump::VertexNormalIndices, 2, [&](detail::BinaryReader& r) {
            const std::uint16_t i = r.u16();
            if (i >= bsp.vertex_normals.size()) {
                in_.fail("vertex normal index out of range");
            }
            bsp.vertex_normal_indices.push_back(i);
        });
        parse_primitives(bsp);
        for (const std::byte b : lump_data(bsp_lump::DispLightmapAlphas)) {
            bsp.disp_lightmap_alphas.push_back(std::to_integer<std::uint8_t>(b));
        }
        for_each(bsp_lump::Nodes, 32, [&](detail::BinaryReader& r) {
            BspNode node;
            node.plane = r.i32();
            node.children = {r.i32(), r.i32()};
            const auto valid = [&](std::int32_t c) {
                return c >= 0 ? static_cast<std::size_t>(c) < lumps_[bsp_lump::Nodes].length / 32
                              : static_cast<std::size_t>(-static_cast<std::int64_t>(c) - 1) < bsp.leafs.size();
            };
            if (node.plane < 0 || static_cast<std::size_t>(node.plane) >= bsp.planes.size() || !valid(node.children[0]) ||
                !valid(node.children[1])) {
                in_.fail("BSP node out of range");
            }
            bsp.nodes.push_back(node);
        });
        const auto pak = lump_data(bsp_lump::PakFile);
        bsp.pakfile.assign(pak.begin(), pak.end());
        return bsp;
    }

private:
    std::span<const std::byte> lump_data(std::size_t index) const {
        return data_.subspan(lumps_[index].offset, lumps_[index].length);
    }

    template <typename Fn> void for_each(std::size_t index, std::size_t record, Fn&& fn) {
        const auto bytes = lump_data(index);
        if (bytes.size() % record != 0) {
            in_.fail("lump " + std::to_string(index) + " size is not a multiple of " + std::to_string(record));
        }
        detail::BinaryReader r(bytes, "BSP lump " + std::to_string(index));
        for (std::size_t i = 0; i < bytes.size() / record; ++i) {
            r.seek(i * record);
            fn(r);
        }
    }

    static Vec3 vec3(detail::BinaryReader& r) {
        const float x = r.f32();
        const float y = r.f32();
        return {x, y, r.f32()};
    }

    void parse_texdata(BspFile& bsp) {
        const auto strings = lump_data(bsp_lump::TexDataStringData);
        std::vector<std::int32_t> table;
        for_each(bsp_lump::TexDataStringTable, 4, [&](detail::BinaryReader& r) { table.push_back(r.i32()); });
        for_each(bsp_lump::TexData, kTexDataSize, [&](detail::BinaryReader& r) {
            BspTexData t;
            t.reflectivity = vec3(r);
            const std::int32_t name_id = r.i32();
            t.width = r.i32();
            t.height = r.i32();
            if (name_id < 0 || static_cast<std::size_t>(name_id) >= table.size() || table[name_id] < 0 ||
                static_cast<std::size_t>(table[name_id]) >= strings.size()) {
                in_.fail("texdata name out of range");
            }
            std::string name;
            for (std::size_t i = static_cast<std::size_t>(table[name_id]);
                 i < strings.size() && strings[i] != std::byte{0}; ++i) {
                name.push_back(static_cast<char>(strings[i]));
            }
            t.material = normalize_game_path(name);
            bsp.texdatas.push_back(std::move(t));
        });
    }

    void parse_faces(BspFile& bsp) {
        for_each(bsp_lump::Faces, kFaceSize, [&](detail::BinaryReader& r) {
            BspFace f;
            for (auto& c : f.average_light) {
                c = r.u32();
            }
            f.plane = r.u16();
            f.side = r.u8();
            f.on_node = r.u8();
            f.first_edge = r.i32();
            f.num_edges = r.u16();
            f.texinfo = r.u16();
            f.dispinfo = static_cast<std::int16_t>(r.u16());
            f.fog_volume = static_cast<std::int16_t>(r.u16());
            for (auto& s : f.styles) {
                s = r.u8();
            }
            r.bytes(16); // Always zero in the shipped maps.
            f.light_offset = r.i32();
            f.area = r.f32();
            f.lightmap_mins = {r.i32(), r.i32()};
            f.lightmap_size = {r.i32(), r.i32()};
            f.original_face = r.i32();
            f.num_primitives = r.u16();
            f.first_primitive = r.u16();

            if (f.plane >= bsp.planes.size() || f.texinfo >= bsp.texinfos.size()) {
                in_.fail("face plane or texinfo out of range");
            }
            if (f.first_edge < 0 || static_cast<std::size_t>(f.first_edge) + f.num_edges > bsp.surf_edges.size()) {
                in_.fail("face edges out of range");
            }
            for (std::size_t e = 0; e < f.num_edges; ++e) {
                const std::int32_t se = bsp.surf_edges[static_cast<std::size_t>(f.first_edge) + e];
                const std::size_t edge = static_cast<std::size_t>(se < 0 ? -static_cast<std::int64_t>(se) : se);
                if (edge >= bsp.edges.size() || bsp.edges[edge][0] >= bsp.vertices.size() ||
                    bsp.edges[edge][1] >= bsp.vertices.size()) {
                    in_.fail("face edge references missing vertex");
                }
            }
            if (f.light_offset >= 0 && f.style_count() > 0) {
                const std::uint64_t luxels = std::uint64_t(f.lightmap_size[0] + 1) * std::uint64_t(f.lightmap_size[1] + 1);
                if (f.lightmap_size[0] < 0 || f.lightmap_size[1] < 0 ||
                    static_cast<std::uint64_t>(f.light_offset) + luxels * 4 * f.style_count() > lumps_[bsp_lump::Lighting].length) {
                    in_.fail("face lightmap out of range");
                }
            }
            bsp.faces.push_back(f);
        });
    }

    void parse_displacements(BspFile& bsp) {
        for_each(bsp_lump::DispVerts, kDispVertSize, [&](detail::BinaryReader& r) {
            BspDispVert v;
            v.vector = vec3(r);
            v.distance = r.f32();
            v.alpha = r.f32();
            bsp.dispverts.push_back(v);
        });
        for_each(bsp_lump::DispInfo, kDispInfoSize, [&](detail::BinaryReader& r) {
            BspDispInfo d;
            d.start_position = vec3(r);
            d.first_vert = r.i32();
            d.first_tri = r.i32();
            d.power = r.i32();
            d.min_tesselation = r.i32();
            d.smoothing_angle = r.f32();
            d.contents = r.i32();
            d.face = r.u16();
            if (d.power < 1 || d.power > 4) {
                in_.fail("displacement power out of range");
            }
            const auto side = static_cast<std::size_t>(d.vertices_per_side());
            if (d.first_vert < 0 || static_cast<std::size_t>(d.first_vert) + side * side > bsp.dispverts.size() ||
                d.face >= bsp.faces.size()) {
                in_.fail("displacement out of range");
            }
            bsp.dispinfos.push_back(d);
        });
        for (const BspFace& f : bsp.faces) {
            if (f.dispinfo >= 0 && static_cast<std::size_t>(f.dispinfo) >= bsp.dispinfos.size()) {
                in_.fail("face references missing displacement");
            }
        }
    }

    void parse_primitives(BspFile& bsp) {
        for_each(bsp_lump::PrimitiveVertices, 12, [&](detail::BinaryReader& r) { bsp.primitive_vertices.push_back(vec3(r)); });
        for_each(bsp_lump::PrimitiveIndices, 2, [&](detail::BinaryReader& r) { bsp.primitive_indices.push_back(r.u16()); });
        for_each(bsp_lump::Primitives, 10, [&](detail::BinaryReader& r) {
            BspPrimitive p;
            p.type = r.u16();
            p.first_index = r.u16();
            p.index_count = r.u16();
            p.first_vertex = r.u16();
            p.vertex_count = r.u16();
            if (std::size_t{p.first_index} + p.index_count > bsp.primitive_indices.size() ||
                std::size_t{p.first_vertex} + p.vertex_count > bsp.primitive_vertices.size()) {
                in_.fail("primitive out of range");
            }
            for (std::size_t i = 0; i < p.index_count; ++i) {
                const std::uint16_t v = bsp.primitive_indices[std::size_t{p.first_index} + i];
                if (v < p.first_vertex || v >= p.first_vertex + p.vertex_count) {
                    in_.fail("primitive index outside its vertices");
                }
            }
            bsp.primitives.push_back(p);
        });
        for (const auto& f : bsp.faces) {
            if (f.num_primitives > 0 && std::size_t{f.first_primitive} + f.num_primitives > bsp.primitives.size()) {
                in_.fail("face primitives out of range");
            }
        }
    }

    void parse_brushes(BspFile& bsp) {
        for_each(bsp_lump::BrushSides, 8, [&](detail::BinaryReader& r) {
            BspBrushSide side;
            side.plane = r.u16();
            side.texinfo = static_cast<std::int16_t>(r.u16());
            side.dispinfo = static_cast<std::int16_t>(r.u16());
            side.bevel = static_cast<std::int16_t>(r.u16());
            if (side.plane >= bsp.planes.size() ||
                (side.texinfo >= 0 && static_cast<std::size_t>(side.texinfo) >= bsp.texinfos.size())) {
                in_.fail("brush side out of range");
            }
            bsp.brush_sides.push_back(side);
        });
        for_each(bsp_lump::Brushes, 12, [&](detail::BinaryReader& r) {
            BspBrush b;
            b.first_side = r.i32();
            b.num_sides = r.i32();
            b.contents = r.i32();
            if (b.first_side < 0 || b.num_sides < 0 ||
                static_cast<std::size_t>(b.first_side) + static_cast<std::size_t>(b.num_sides) > bsp.brush_sides.size()) {
                in_.fail("brush sides out of range");
            }
            bsp.brushes.push_back(b);
        });
        for_each(bsp_lump::LeafBrushes, 2, [&](detail::BinaryReader& r) {
            const std::uint16_t b = r.u16();
            if (b >= bsp.brushes.size()) {
                in_.fail("leaf brush out of range");
            }
            bsp.leaf_brushes.push_back(b);
        });
    }

    void parse_areas(BspFile& bsp) {
        for_each(bsp_lump::ClipPortalVerts, 12, [&](detail::BinaryReader& r) { bsp.clip_portal_verts.push_back(vec3(r)); });
        for_each(bsp_lump::AreaPortals, 12, [&](detail::BinaryReader& r) {
            BspAreaPortal a;
            a.key = r.u16();
            a.other_area = r.u16();
            a.first_clip_vert = r.u16();
            a.num_clip_verts = r.u16();
            a.plane = r.i32();
            if (std::size_t{a.first_clip_vert} + a.num_clip_verts > bsp.clip_portal_verts.size() || a.plane < 0 ||
                static_cast<std::size_t>(a.plane) >= bsp.planes.size()) {
                in_.fail("area portal out of range");
            }
            bsp.area_portals.push_back(a);
        });
        for_each(bsp_lump::Areas, 8, [&](detail::BinaryReader& r) {
            BspArea a;
            a.num_portals = r.i32();
            a.first_portal = r.i32();
            if (a.num_portals < 0 || a.first_portal < 0 ||
                static_cast<std::size_t>(a.first_portal) + static_cast<std::size_t>(a.num_portals) > bsp.area_portals.size()) {
                in_.fail("area portals out of range");
            }
            bsp.areas.push_back(a);
        });
        for (const auto& portal : bsp.area_portals) {
            if (portal.other_area >= bsp.areas.size()) {
                in_.fail("area portal leads to a missing area");
            }
        }
    }

    // Header: i32 cluster count, then per cluster i32 PVS and PAS offsets
    // into the lump. Rows are run-length compressed (see decompress).
    void parse_visibility(BspFile& bsp) {
        const auto bytes = lump_data(bsp_lump::Visibility);
        if (bytes.empty()) {
            return;
        }
        detail::BinaryReader r(bytes, "BSP visibility");
        auto& vis = bsp.visibility;
        vis.clusters = r.i32();
        if (vis.clusters < 0 || static_cast<std::uint64_t>(vis.clusters) * 8 > r.remaining()) {
            in_.fail("visibility cluster count out of range");
        }
        for (std::int32_t c = 0; c < vis.clusters; ++c) {
            const std::int32_t pvs = r.i32();
            const std::int32_t pas = r.i32();
            if (pvs < 0 || pas < 0 || static_cast<std::size_t>(pvs) >= bytes.size() ||
                static_cast<std::size_t>(pas) >= bytes.size()) {
                in_.fail("visibility offset out of range");
            }
            vis.offsets.push_back({pvs, pas});
        }
        vis.data.assign(bytes.begin(), bytes.end());
        for (const auto& leaf : bsp.leafs) {
            if (leaf.cluster >= vis.clusters) {
                in_.fail("leaf cluster out of range");
            }
        }
    }

    // Collision for brush models: blocks of {i32 model, i32 data size,
    // i32 text size, i32 solid count}, the solids (each i32 size + data, as in
    // .phy files), then KeyValues text; a model of -1 ends the list.
    void parse_collision(BspFile& bsp) {
        const auto bytes = lump_data(bsp_lump::PhysCollide);
        detail::BinaryReader r(bytes, "BSP collision");
        std::size_t at = 0;
        while (at + 16 <= bytes.size()) {
            r.seek(at);
            const std::int32_t model = r.i32();
            if (model == -1) {
                return;
            }
            const std::int32_t data_size = r.i32();
            const std::int32_t text_size = r.i32();
            const std::int32_t solids = r.i32();
            if (model < 0 || static_cast<std::size_t>(model) >= bsp.models.size() || data_size < 0 || text_size < 0 ||
                solids < 0 || static_cast<std::uint64_t>(data_size) + static_cast<std::uint64_t>(text_size) >
                                  bytes.size() - (at + 16)) {
                in_.fail("collision block out of range");
            }
            BspCollisionModel cm;
            cm.model = model;
            std::size_t solid = at + 16;
            const std::size_t data_end = solid + static_cast<std::size_t>(data_size);
            for (std::int32_t k = 0; k < solids; ++k) {
                r.seek(solid);
                const std::int32_t size = r.i32();
                if (size < 0 || solid + 4 + static_cast<std::size_t>(size) > data_end) {
                    in_.fail("collision solid out of range");
                }
                cm.solids.push_back(detail::parse_physics_solid(bytes, solid + 4, static_cast<std::size_t>(size), r));
                solid += 4 + static_cast<std::size_t>(size);
            }
            if (solid != data_end) {
                in_.fail("collision solids do not fill their block");
            }
            std::string_view text(reinterpret_cast<const char*>(bytes.data() + data_end), static_cast<std::size_t>(text_size));
            if (const auto nul = text.find('\0'); nul != std::string_view::npos) {
                text = text.substr(0, nul);
            }
            cm.text = parse_keyvalues(text);
            bsp.collision.push_back(std::move(cm));
            at = data_end + static_cast<std::size_t>(text_size);
        }
        if (!bytes.empty()) {
            in_.fail("collision lump has no end marker");
        }
    }

    // Detail props, version 2: model names (128 bytes each), then 40-byte
    // props. There is no sprite dictionary in this version.
    void parse_detail_props(BspFile& bsp, std::span<const std::byte> bytes) {
        detail::BinaryReader r(bytes, "BSP detail props");
        const std::int32_t names = r.i32();
        if (names < 0 || static_cast<std::uint64_t>(names) * kStaticPropNameSize > r.remaining()) {
            in_.fail("detail prop model count out of range");
        }
        for (std::int32_t n = 0; n < names; ++n) {
            const auto raw = r.bytes(kStaticPropNameSize);
            std::string name;
            for (std::byte b : raw) {
                if (b == std::byte{0}) {
                    break;
                }
                name.push_back(static_cast<char>(b));
            }
            bsp.detail_prop_models.push_back(normalize_game_path(name));
        }
        const std::int32_t props = r.i32();
        // Maps recompiled by the Unofficial Patch carry 4 trailing bytes
        // (usually zero, sometimes leftover memory) after the props.
        if (props < 0 || (static_cast<std::uint64_t>(props) * 40 != r.remaining() &&
                          static_cast<std::uint64_t>(props) * 40 + 4 != r.remaining())) {
            in_.fail("detail prop count does not match the lump size");
        }
        for (std::int32_t p = 0; p < props; ++p) {
            const std::size_t start = r.pos();
            BspDetailProp d;
            d.origin = vec3(r);
            d.angles = vec3(r);
            d.model = r.u16();
            d.leaf = r.u16();
            for (auto& c : d.lighting) {
                c = r.u8();
            }
            d.first_light_style = r.u32();
            d.light_style_count = r.u8();
            // Three more bytes, often 0xCD (uninitialised memory in the compiler).
            if (d.model >= bsp.detail_prop_models.size()) {
                in_.fail("detail prop references missing model");
            }
            bsp.detail_props.push_back(d);
            r.seek(start + 40);
        }
    }

    // Detail prop light styles: i32 count, then 5-byte {ColorRGBExp32, style}.
    void parse_detail_light_styles(BspFile& bsp, std::span<const std::byte> bytes) {
        detail::BinaryReader r(bytes, "BSP detail prop lighting");
        const std::int32_t count = r.i32();
        if (count < 0 || static_cast<std::uint64_t>(count) * 5 != r.remaining()) {
            in_.fail("detail light style count does not match the lump size");
        }
        for (std::int32_t i = 0; i < count; ++i) {
            BspDetailLightStyle s;
            for (auto& c : s.lighting) {
                c = r.u8();
            }
            s.style = r.u8();
            bsp.detail_light_styles.push_back(s);
        }
    }

    void parse_game_lump(BspFile& bsp) {
        const auto bytes = lump_data(bsp_lump::GameLump);
        if (bytes.empty()) {
            return;
        }
        detail::BinaryReader dir(bytes, "BSP game lump");
        const std::int32_t count = dir.i32();
        for (std::int32_t i = 0; i < count; ++i) {
            const std::uint32_t id = dir.u32();
            dir.u16(); // flags
            const std::uint16_t version = dir.u16();
            const std::int32_t offset = dir.i32();
            const std::int32_t length = dir.i32();
            if (offset < 0 || length < 0 || static_cast<std::uint64_t>(offset) + length > data_.size()) {
                in_.fail("game lump out of range");
            }
            const auto sub = data_.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
            if (id == 0x64707270u) { // 'dprp'
                if (version != 2) {
                    in_.fail("unsupported detail prop version " + std::to_string(version));
                }
                parse_detail_props(bsp, sub);
                continue;
            }
            if (id == 0x64706c74u) { // 'dplt'
                parse_detail_light_styles(bsp, sub);
                continue;
            }
            if (id != 0x73707270u) { // 'sprp'
                continue;
            }
            if (version != 4) {
                in_.fail("unsupported static prop version " + std::to_string(version));
            }
            if (offset < 0 || length < 0 || static_cast<std::uint64_t>(offset) + length > data_.size()) {
                in_.fail("static prop lump out of range");
            }
            detail::BinaryReader r(data_.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(length)),
                                   "BSP static props");
            const std::int32_t names = r.i32();
            for (std::int32_t n = 0; n < names; ++n) {
                const auto raw = r.bytes(kStaticPropNameSize);
                std::string name;
                for (std::byte b : raw) {
                    if (b == std::byte{0}) {
                        break;
                    }
                    name.push_back(static_cast<char>(b));
                }
                bsp.static_prop_models.push_back(normalize_game_path(name));
            }
            const std::int32_t leafs = r.i32();
            r.bytes(static_cast<std::size_t>(std::max(leafs, 0)) * 2);
            const std::int32_t props = r.i32();
            if (props < 0 || static_cast<std::uint64_t>(props) * kStaticPropSize > r.remaining()) {
                in_.fail("static prop count out of range");
            }
            for (std::int32_t p = 0; p < props; ++p) {
                BspStaticProp s;
                s.origin = vec3(r);
                s.angles = vec3(r);
                s.model = r.u16();
                s.first_leaf = r.u16();
                s.leaf_count = r.u16();
                s.solid = r.u8();
                s.flags = r.u8();
                s.skin = r.i32();
                s.fade_min = r.f32();
                s.fade_max = r.f32();
                s.lighting_origin = vec3(r);
                if (s.model >= bsp.static_prop_models.size()) {
                    in_.fail("static prop references missing model");
                }
                bsp.static_props.push_back(s);
            }
        }
    }

    std::span<const std::byte> data_;
    detail::BinaryReader in_;
    std::array<Lump, bsp_lump::Count> lumps_{};
};

} // namespace

std::vector<std::uint8_t> BspVisibility::decompress(std::int32_t cluster, int kind) const {
    if (cluster < 0 || cluster >= clusters || kind < 0 || kind > 1) {
        throw FormatError("BSP visibility: cluster out of range");
    }
    const std::size_t bytes = (static_cast<std::size_t>(clusters) + 7) / 8;
    std::vector<std::uint8_t> out;
    out.reserve(bytes);
    // A zero byte is followed by a count of zero bytes; other bytes are literal.
    std::size_t i = static_cast<std::size_t>(offsets[static_cast<std::size_t>(cluster)][static_cast<std::size_t>(kind)]);
    while (out.size() < bytes) {
        if (i >= data.size()) {
            throw FormatError("BSP visibility: row runs past the end of the data");
        }
        const auto b = std::to_integer<std::uint8_t>(data[i++]);
        if (b != 0) {
            out.push_back(b);
            continue;
        }
        if (i >= data.size()) {
            throw FormatError("BSP visibility: row runs past the end of the data");
        }
        out.insert(out.end(), std::to_integer<std::uint8_t>(data[i++]), 0);
    }
    if (out.size() != bytes) {
        throw FormatError("BSP visibility: row overruns the cluster count");
    }
    return out;
}

namespace {

Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

std::array<float, 2> texture_uv(const BspFile& bsp, const BspTexInfo& ti, const Vec3& p) {
    float w = 1.0f;
    float h = 1.0f;
    if (ti.texdata >= 0 && static_cast<std::size_t>(ti.texdata) < bsp.texdatas.size()) {
        const auto& td = bsp.texdatas[static_cast<std::size_t>(ti.texdata)];
        w = td.width > 0 ? static_cast<float>(td.width) : 1.0f;
        h = td.height > 0 ? static_cast<float>(td.height) : 1.0f;
    }
    const auto& s = ti.texture_vecs[0];
    const auto& t = ti.texture_vecs[1];
    return {(p[0] * s[0] + p[1] * s[1] + p[2] * s[2] + s[3]) / w, (p[0] * t[0] + p[1] * t[1] + p[2] * t[2] + t[3]) / h};
}

std::array<float, 2> lightmap_uv(const BspFace& face, const BspTexInfo& ti, const Vec3& p) {
    const auto& s = ti.lightmap_vecs[0];
    const auto& t = ti.lightmap_vecs[1];
    return {p[0] * s[0] + p[1] * s[1] + p[2] * s[2] + s[3] - static_cast<float>(face.lightmap_mins[0]),
            p[0] * t[0] + p[1] * t[1] + p[2] * t[2] + t[3] - static_cast<float>(face.lightmap_mins[1])};
}

} // namespace

std::array<float, 3> decode_rgbe(const std::byte* luxel) {
    const float scale = std::ldexp(1.0f, static_cast<std::int8_t>(luxel[3])) / 255.0f;
    return {static_cast<float>(luxel[0]) * scale, static_cast<float>(luxel[1]) * scale,
            static_cast<float>(luxel[2]) * scale};
}

std::size_t BspFace::style_count() const {
    std::size_t n = 0;
    while (n < styles.size() && styles[n] != 255) {
        ++n;
    }
    return n;
}

std::vector<Vec3> BspFile::face_polygon(const BspFace& face) const {
    std::vector<Vec3> out;
    out.reserve(face.num_edges);
    for (std::size_t e = 0; e < face.num_edges; ++e) {
        const std::int32_t se = surf_edges[static_cast<std::size_t>(face.first_edge) + e];
        const auto& edge = edges[static_cast<std::size_t>(se < 0 ? -static_cast<std::int64_t>(se) : se)];
        out.push_back(vertices[se >= 0 ? edge[0] : edge[1]]);
    }
    return out;
}

std::size_t BspFile::leaf_at(const Vec3& point) const {
    std::int32_t node = models.empty() ? 0 : models[0].head_node;
    while (node >= 0 && static_cast<std::size_t>(node) < nodes.size()) {
        const BspNode& n = nodes[static_cast<std::size_t>(node)];
        const BspPlane& p = planes[static_cast<std::size_t>(n.plane)];
        const float d = p.normal[0] * point[0] + p.normal[1] * point[1] + p.normal[2] * point[2] - p.dist;
        node = n.children[d >= 0 ? 0 : 1];
    }
    return node < 0 ? static_cast<std::size_t>(-static_cast<std::int64_t>(node) - 1) : 0;
}

namespace {

// Recursive segment test through the BSP tree.
bool trace_node(const BspFile& bsp, std::int32_t node, const Vec3& a, const Vec3& b, std::int32_t mask, int depth) {
    if (node < 0) {
        const auto leaf = static_cast<std::size_t>(-static_cast<std::int64_t>(node) - 1);
        return leaf < bsp.leafs.size() && (bsp.leafs[leaf].contents & mask) != 0;
    }
    if (depth > 256 || static_cast<std::size_t>(node) >= bsp.nodes.size()) {
        return false;
    }
    const BspNode& n = bsp.nodes[static_cast<std::size_t>(node)];
    const BspPlane& p = bsp.planes[static_cast<std::size_t>(n.plane)];
    const float da = p.normal[0] * a[0] + p.normal[1] * a[1] + p.normal[2] * a[2] - p.dist;
    const float db = p.normal[0] * b[0] + p.normal[1] * b[1] + p.normal[2] * b[2] - p.dist;
    if (da >= 0 && db >= 0) {
        return trace_node(bsp, n.children[0], a, b, mask, depth + 1);
    }
    if (da < 0 && db < 0) {
        return trace_node(bsp, n.children[1], a, b, mask, depth + 1);
    }
    const float t = da / (da - db);
    const Vec3 mid = lerp(a, b, t);
    const int near_side = da >= 0 ? 0 : 1;
    return trace_node(bsp, n.children[near_side], a, mid, mask, depth + 1) ||
           trace_node(bsp, n.children[1 - near_side], mid, b, mask, depth + 1);
}

} // namespace

bool BspFile::segment_blocked(const Vec3& a, const Vec3& b, std::int32_t mask) const {
    if (nodes.empty() || models.empty()) {
        return false;
    }
    return trace_node(*this, models[0].head_node, a, b, mask, 0);
}

BspFile parse_bsp(std::span<const std::byte> data) {
    return Parser(data).run();
}

std::vector<BspEntity> parse_bsp_entities(std::string_view text) {
    std::vector<BspEntity> out;
    std::size_t pos = 0;
    // Also skips "//" comment lines, which the Unofficial Patch uses to disable
    // entity outputs (la_bradbury_2).
    auto skip_space = [&] {
        for (;;) {
            while (pos < text.size() &&
                   (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) {
                ++pos;
            }
            if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '/') {
                while (pos < text.size() && text[pos] != '\n') {
                    ++pos;
                }
                continue;
            }
            return;
        }
    };
    // Values may contain \" (Python code in entity I/O); other backslashes are literal.
    auto quoted = [&](std::string& value) {
        skip_space();
        if (pos >= text.size() || text[pos] != '"') {
            return false;
        }
        value.clear();
        for (++pos; pos < text.size(); ++pos) {
            if (text[pos] == '\\' && pos + 1 < text.size() && text[pos + 1] == '"') {
                value.push_back('"');
                ++pos;
            } else if (text[pos] == '"') {
                ++pos;
                return true;
            } else {
                value.push_back(text[pos]);
            }
        }
        throw FormatError("BSP entities: unterminated string");
    };
    for (;;) {
        skip_space();
        if (pos >= text.size()) {
            break;
        }
        if (text[pos] != '{') {
            throw FormatError("BSP entities: expected '{'");
        }
        ++pos;
        BspEntity entity;
        for (;;) {
            skip_space();
            if (pos >= text.size()) {
                throw FormatError("BSP entities: unterminated entity");
            }
            if (text[pos] == '}') {
                ++pos;
                break;
            }
            std::string key;
            std::string value;
            if (!quoted(key) || !quoted(value)) {
                throw FormatError("BSP entities: expected quoted key and value");
            }
            // Text glued to a closing quote is an editing slip (the Unofficial
            // Patch's sp_giovanni_2b has `"...,"mop`); skip it to the end of the line.
            if (pos < text.size() && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\r' &&
                text[pos] != '\n' && text[pos] != '"' && text[pos] != '}') {
                while (pos < text.size() && text[pos] != '\n') {
                    ++pos;
                }
            }
            entity[key] = std::move(value);
        }
        out.push_back(std::move(entity));
    }
    return out;
}

BspFaceMesh build_face_mesh(const BspFile& bsp, std::size_t face_index) {
    const BspFace& face = bsp.faces.at(face_index);
    const BspTexInfo& ti = bsp.texinfos[face.texinfo];
    const auto polygon = bsp.face_polygon(face);
    BspFaceMesh mesh;

    if (face.dispinfo < 0 && face.num_primitives > 0) {
        // T-junction-free triangulation: triangle strips over extra vertices on
        // the face's plane. Strip winding alternates, so each triangle is
        // oriented to match the polygon's winding (its Newell normal).
        Vec3 normal{};
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Vec3& a = polygon[i];
            const Vec3& b = polygon[(i + 1) % polygon.size()];
            normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
            normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
            normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
        }
        for (std::uint16_t k = 0; k < face.num_primitives; ++k) {
            const BspPrimitive& prim = bsp.primitives[std::size_t{face.first_primitive} + k];
            const auto base = static_cast<std::uint32_t>(mesh.positions.size());
            for (std::uint16_t v = 0; v < prim.vertex_count; ++v) {
                const Vec3& p = bsp.primitive_vertices[std::size_t{prim.first_vertex} + v];
                mesh.positions.push_back(p);
                mesh.uvs.push_back(texture_uv(bsp, ti, p));
                mesh.lightmap_uvs.push_back(lightmap_uv(face, ti, p));
                mesh.alphas.push_back(0.0f);
            }
            for (std::uint16_t i = 0; i + 2 < prim.index_count; ++i) {
                std::array<std::uint32_t, 3> t{};
                for (std::size_t c = 0; c < 3; ++c) {
                    t[c] = base + bsp.primitive_indices[std::size_t{prim.first_index} + i + c] - prim.first_vertex;
                }
                if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) {
                    continue; // Degenerate join between strip runs.
                }
                const Vec3& a = mesh.positions[t[0]];
                const Vec3& b = mesh.positions[t[1]];
                const Vec3& c = mesh.positions[t[2]];
                const Vec3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const Vec3 w{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const Vec3 n{u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
                if (n[0] * normal[0] + n[1] * normal[1] + n[2] * normal[2] < 0) {
                    std::swap(t[1], t[2]);
                }
                mesh.indices.insert(mesh.indices.end(), t.begin(), t.end());
            }
        }
        if (!mesh.indices.empty()) {
            return mesh;
        }
        mesh = {};
    }

    if (face.dispinfo < 0) {
        for (const Vec3& p : polygon) {
            mesh.positions.push_back(p);
            mesh.uvs.push_back(texture_uv(bsp, ti, p));
            mesh.lightmap_uvs.push_back(lightmap_uv(face, ti, p));
            mesh.alphas.push_back(0.0f);
        }
        for (std::uint32_t i = 2; i < polygon.size(); ++i) {
            mesh.indices.insert(mesh.indices.end(), {0u, i - 1, i});
        }
        return mesh;
    }

    const BspDispInfo& disp = bsp.dispinfos[static_cast<std::size_t>(face.dispinfo)];
    if (polygon.size() != 4) {
        throw FormatError("displacement face is not a quad");
    }
    // Rotate the corners so that the one nearest the start position comes first.
    std::size_t first = 0;
    float best = INFINITY;
    for (std::size_t i = 0; i < 4; ++i) {
        float d = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            d += (polygon[i][k] - disp.start_position[k]) * (polygon[i][k] - disp.start_position[k]);
        }
        if (d < best) {
            best = d;
            first = i;
        }
    }
    std::array<Vec3, 4> c;
    for (std::size_t i = 0; i < 4; ++i) {
        c[i] = polygon[(first + i) % 4];
    }

    const auto n = static_cast<std::uint32_t>(disp.vertices_per_side());
    const float step = 1.0f / static_cast<float>(n - 1);
    for (std::uint32_t row = 0; row < n; ++row) {
        const Vec3 a = lerp(c[0], c[1], static_cast<float>(row) * step);
        const Vec3 b = lerp(c[3], c[2], static_cast<float>(row) * step);
        for (std::uint32_t col = 0; col < n; ++col) {
            const Vec3 base = lerp(a, b, static_cast<float>(col) * step);
            const BspDispVert& dv = bsp.dispverts[static_cast<std::size_t>(disp.first_vert) + row * n + col];
            mesh.positions.push_back({base[0] + dv.vector[0] * dv.distance, base[1] + dv.vector[1] * dv.distance,
                                      base[2] + dv.vector[2] * dv.distance});
            mesh.uvs.push_back(texture_uv(bsp, ti, base));
            mesh.lightmap_uvs.push_back(lightmap_uv(face, ti, base));
            mesh.alphas.push_back(dv.alpha / 255.0f);
        }
    }
    for (std::uint32_t row = 0; row + 1 < n; ++row) {
        for (std::uint32_t col = 0; col + 1 < n; ++col) {
            const std::uint32_t i0 = row * n + col;
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + n;
            const std::uint32_t i3 = i2 + 1;
            // Alternate the diagonal so the grid is symmetric.
            if ((row + col) % 2 == 0) {
                mesh.indices.insert(mesh.indices.end(), {i0, i2, i3, i0, i3, i1});
            } else {
                mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
            }
        }
    }
    return mesh;
}

} // namespace openbl::formats
