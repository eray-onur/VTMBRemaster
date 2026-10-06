#include "openbl/formats/bsp.hpp"

#include "binary_reader.hpp"
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
        for_each(bsp_lump::Leafs, 32, [&](detail::BinaryReader& r) {
            BspLeaf leaf;
            leaf.contents = r.i32();
            leaf.cluster = static_cast<std::int16_t>(r.u16());
            bsp.leafs.push_back(leaf);
        });
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
    auto skip_space = [&] {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) {
            ++pos;
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
