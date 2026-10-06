#pragma once

// Bloodlines maps (VBSP version 17). See docs/formats/maps.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace openbl::formats {

constexpr std::int32_t kBspVersion = 17;

using Vec3 = std::array<float, 3>;

namespace bsp_lump {
enum : std::size_t {
    Entities = 0,
    Planes = 1,
    TexData = 2,
    Vertices = 3,
    Visibility = 4,
    Nodes = 5,
    TexInfo = 6,
    Faces = 7,
    Lighting = 8,
    Leafs = 10,
    Edges = 12,
    SurfEdges = 13,
    Models = 14,
    WorldLights = 15,
    DispInfo = 26,
    OriginalFaces = 27,
    DispVerts = 33,
    GameLump = 35,
    PakFile = 40,
    Cubemaps = 42,
    TexDataStringData = 43,
    TexDataStringTable = 44,
    Count = 64,
};
}

namespace surf_flags {
constexpr std::int32_t Light = 0x1;
constexpr std::int32_t Sky2D = 0x2;
constexpr std::int32_t Sky = 0x4;
constexpr std::int32_t Warp = 0x8;
constexpr std::int32_t Trans = 0x10;
constexpr std::int32_t Trigger = 0x40;
constexpr std::int32_t NoDraw = 0x80;
constexpr std::int32_t Hint = 0x100;
constexpr std::int32_t Skip = 0x200;
constexpr std::int32_t NoLight = 0x400;
} // namespace surf_flags

struct BspPlane {
    Vec3 normal{};
    float dist = 0;
    std::int32_t type = 0;
};

struct BspTexInfo {
    std::array<std::array<float, 4>, 2> texture_vecs{};  // u, v in texels: dot(pos, xyz) + w
    std::array<std::array<float, 4>, 2> lightmap_vecs{}; // in luxels
    std::int32_t flags = 0;
    std::int32_t texdata = -1;
};

struct BspTexData {
    Vec3 reflectivity{};
    std::string material; // Lowercase, '/' separators, relative to materials/.
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// Bloodlines faces are 104 bytes: Valve's fields plus per-style average light
// colors at the front and 8 lightmap styles instead of 4.
struct BspFace {
    std::array<std::uint32_t, 8> average_light{}; // ColorRGBExp32 per style.
    std::uint16_t plane = 0;
    std::uint8_t side = 0;
    std::uint8_t on_node = 0;
    std::int32_t first_edge = 0;
    std::uint16_t num_edges = 0;
    std::uint16_t texinfo = 0;
    std::int16_t dispinfo = -1;
    std::int16_t fog_volume = -1;
    std::array<std::uint8_t, 8> styles{}; // 255 = unused.
    std::int32_t light_offset = -1;       // Byte offset into the lighting lump.
    float area = 0;
    std::array<std::int32_t, 2> lightmap_mins{};
    std::array<std::int32_t, 2> lightmap_size{}; // Luxels minus one.
    std::int32_t original_face = 0;
    std::uint16_t num_primitives = 0;
    std::uint16_t first_primitive = 0;

    std::size_t style_count() const;
};

struct BspModel {
    Vec3 mins{};
    Vec3 maxs{};
    Vec3 origin{};
    std::int32_t head_node = 0;
    std::int32_t first_face = 0;
    std::int32_t num_faces = 0;
};

struct BspDispInfo {
    Vec3 start_position{};
    std::int32_t first_vert = 0;
    std::int32_t first_tri = 0;
    std::int32_t power = 0; // 2^power + 1 vertices per side.
    std::int32_t min_tesselation = 0;
    float smoothing_angle = 0;
    std::int32_t contents = 0;
    std::uint16_t face = 0;

    std::int32_t vertices_per_side() const { return (1 << power) + 1; }
};

struct BspDispVert {
    Vec3 vector{};
    float distance = 0;
    float alpha = 0;
};

struct BspStaticProp {
    Vec3 origin{};
    Vec3 angles{}; // Pitch, yaw, roll in degrees.
    std::uint16_t model = 0; // Index into BspFile::static_prop_models.
    std::uint16_t first_leaf = 0;
    std::uint16_t leaf_count = 0;
    std::uint8_t solid = 0;
    std::uint8_t flags = 0;
    std::int32_t skin = 0;
    float fade_min = 0;
    float fade_max = 0;
    Vec3 lighting_origin{};
};

using BspEntity = std::map<std::string, std::string>;

struct BspFile {
    std::int32_t version = 0;
    std::int32_t map_revision = 0;
    std::vector<BspEntity> entities;
    std::vector<BspPlane> planes;
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint16_t, 2>> edges;
    std::vector<std::int32_t> surf_edges;
    std::vector<BspTexInfo> texinfos;
    std::vector<BspTexData> texdatas;
    std::vector<BspFace> faces;
    std::vector<BspModel> models;
    std::vector<std::byte> lighting; // ColorRGBExp32 luxels.
    std::vector<BspDispInfo> dispinfos;
    std::vector<BspDispVert> dispverts;
    std::vector<std::string> static_prop_models;
    std::vector<BspStaticProp> static_props;
    std::vector<std::byte> pakfile; // Uncompressed zip; see ZipArchive.

    // Vertex positions of a face's polygon, in winding order.
    std::vector<Vec3> face_polygon(const BspFace& face) const;
};

// Throws FormatError.
BspFile parse_bsp(std::span<const std::byte> data);

// Parses the entity lump text: a sequence of { "key" "value" ... } blocks.
std::vector<BspEntity> parse_bsp_entities(std::string_view text);

// Triangulated geometry for one face: positions, texture UVs (normalized by
// the texdata size) and displacement alpha. Displacements are tessellated.
struct BspFaceMesh {
    std::vector<Vec3> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<float> alphas;
    std::vector<std::uint32_t> indices;
};
BspFaceMesh build_face_mesh(const BspFile& bsp, std::size_t face_index);

} // namespace openbl::formats
