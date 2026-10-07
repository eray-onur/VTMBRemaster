#pragma once

// Bloodlines maps (VBSP version 17). See docs/formats/maps.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/phy.hpp"

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
    LeafFaces = 16,
    LeafBrushes = 17,
    Brushes = 18,
    BrushSides = 19,
    Areas = 20,
    AreaPortals = 21,
    DispInfo = 26,
    OriginalFaces = 27,
    PhysCollide = 29,
    VertexNormals = 30,
    VertexNormalIndices = 31,
    DispLightmapAlphas = 32,
    DispLightmapSamplePositions = 34,
    DispVerts = 33,
    GameLump = 35,
    LeafWaterData = 36,
    Primitives = 37,
    PrimitiveVertices = 38,
    PrimitiveIndices = 39,
    PakFile = 40,
    ClipPortalVerts = 41,
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

enum class BspLightType : std::int32_t {
    Surface = 0,    // Emissive surface (area light), approximated as a point with a facing.
    Point = 1,
    Spot = 2,
    Sky = 3,        // Directional sun light.
    QuakeLight = 4,
    SkyAmbient = 5,
};

// World light (88 bytes in Bloodlines maps, as in Valve's dworldlight_t).
// Intensity is in the same linear units as the lightmaps: a light contributes
// intensity * max(0, n.l) / (constant + linear*d + quadratic*d^2) to a surface.
struct BspWorldLight {
    Vec3 origin{};
    Vec3 intensity{};
    Vec3 normal{}; // Spot/surface/sky direction.
    std::int32_t cluster = -1;
    BspLightType type = BspLightType::Point;
    std::int32_t style = 0;
    float stopdot = 0;  // Spot: cosine of the inner cone.
    float stopdot2 = 0; // Spot: cosine of the outer cone.
    float exponent = 0;
    float radius = 0;   // 0 = unlimited.
    float constant_attn = 0;
    float linear_attn = 0;
    float quadratic_attn = 0;
    std::int32_t flags = 0;
    std::int32_t texinfo = -1;
    std::int32_t owner = 0;
};

struct BspNode {
    std::int32_t plane = 0;
    std::array<std::int32_t, 2> children{}; // >= 0: node; < 0: leaf -(index + 1).
};

namespace contents {
constexpr std::int32_t Solid = 0x1;
constexpr std::int32_t Window = 0x2;
constexpr std::int32_t Water = 0x20;
} // namespace contents

struct BspLeaf {
    std::int32_t contents = 0;
    std::int16_t cluster = -1; // -1: outside the visibility data.
    std::uint16_t area = 0;
    std::uint16_t flags = 0;
    std::array<std::int16_t, 3> mins{}, maxs{};
    std::uint16_t first_leaf_face = 0, num_leaf_faces = 0;
    std::uint16_t first_leaf_brush = 0, num_leaf_brushes = 0;
    std::int16_t water_data = -1; // Index into BspFile::leaf_water, or -1.
};

struct BspBrush {
    std::int32_t first_side = 0, num_sides = 0;
    std::int32_t contents = 0;
};

struct BspBrushSide {
    std::uint16_t plane = 0;
    std::int16_t texinfo = -1;
    std::int16_t dispinfo = 0;
    std::int16_t bevel = 0; // 1 for bevel planes added by the compiler.
};

struct BspArea {
    std::int32_t num_portals = 0, first_portal = 0;
};

struct BspAreaPortal {
    std::uint16_t key = 0;        // Matches a func_areaportal's portal number.
    std::uint16_t other_area = 0;
    std::uint16_t first_clip_vert = 0, num_clip_verts = 0;
    std::int32_t plane = 0;
};

struct BspLeafWater {
    float surface_z = 0, min_z = 0;
    std::int16_t texinfo = -1;
};

struct BspCubemap {
    std::array<std::int32_t, 3> origin{};
    std::int32_t size = 0; // 0 = default resolution.
};

struct BspDetailProp {
    Vec3 origin{};
    Vec3 angles{};
    std::uint16_t model = 0; // Index into BspFile::detail_prop_models.
    std::uint16_t leaf = 0;
    std::array<std::uint8_t, 4> lighting{}; // ColorRGBExp32.
    std::uint32_t first_light_style = 0xFFFFFFFF; // Into detail_light_styles, or none.
    std::uint8_t light_style_count = 0;
};

struct BspDetailLightStyle {
    std::array<std::uint8_t, 4> lighting{}; // ColorRGBExp32.
    std::uint8_t style = 0;
};

// Replacement triangulation for a face, with extra vertices on the face's
// plane that close T-junction cracks. Indices are absolute positions in
// BspFile::primitive_vertices, within [first_vertex, first_vertex + vertex_count).
struct BspPrimitive {
    std::uint16_t type = 1; // 1 = triangle strip (the only type in the shipped maps).
    std::uint16_t first_index = 0, index_count = 0;
    std::uint16_t first_vertex = 0, vertex_count = 0;
};

// Where a displacement lightmap luxel was sampled: a triangle of the
// displacement and barycentric weights in 0..255 (summing to 253-255).
struct BspDispSample {
    std::uint16_t triangle = 0;
    std::array<std::uint8_t, 3> barycentric{};
};

// Collision for one brush model (model 0 is the world).
struct BspCollisionModel {
    std::int32_t model = 0;
    std::vector<PhySolid> solids;
    KvDocument text; // "solid", "staticsolid", "fluid", "materialtable" blocks.
};

// Potentially visible / audible sets, run-length compressed per cluster.
struct BspVisibility {
    std::int32_t clusters = 0;
    std::vector<std::array<std::int32_t, 2>> offsets; // Per cluster: PVS, PAS byte offsets into data.
    std::vector<std::byte> data;                       // The whole lump.

    // One bit per cluster. kind 0 = visible (PVS), 1 = audible (PAS).
    // Throws FormatError on corrupt data.
    std::vector<std::uint8_t> decompress(std::int32_t cluster, int kind = 0) const;
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
    std::vector<BspWorldLight> world_lights;
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leafs;
    std::vector<std::uint16_t> leaf_faces;
    std::vector<std::uint16_t> leaf_brushes;
    std::vector<BspBrush> brushes;
    std::vector<BspBrushSide> brush_sides;
    std::vector<BspArea> areas;
    std::vector<BspAreaPortal> area_portals;
    std::vector<Vec3> clip_portal_verts;
    std::vector<BspLeafWater> leaf_water;
    std::vector<BspCubemap> cubemaps;
    BspVisibility visibility;
    std::vector<std::string> detail_prop_models;
    std::vector<BspDetailProp> detail_props;
    std::vector<BspDetailLightStyle> detail_light_styles;
    std::vector<BspCollisionModel> collision;
    // One normal index per face corner, in face order (faces' surfedges concatenated).
    std::vector<Vec3> vertex_normals;
    std::vector<std::uint16_t> vertex_normal_indices;
    std::vector<BspPrimitive> primitives;
    std::vector<Vec3> primitive_vertices;
    std::vector<std::uint16_t> primitive_indices;
    std::vector<std::uint8_t> disp_lightmap_alphas; // One byte per displacement lightmap luxel.
    // One per displacement lightmap luxel, displacement faces in face order.
    std::vector<BspDispSample> disp_lightmap_samples;
    std::vector<std::string> static_prop_models;
    std::vector<BspStaticProp> static_props;
    std::vector<std::byte> pakfile; // Uncompressed zip; see ZipArchive.

    // Vertex positions of a face's polygon, in winding order.
    std::vector<Vec3> face_polygon(const BspFace& face) const;

    // Index of the world leaf containing `point`.
    std::size_t leaf_at(const Vec3& point) const;
    // True if the segment from `a` to `b` passes through a world leaf whose
    // contents intersect `mask` (solid by default).
    bool segment_blocked(const Vec3& a, const Vec3& b, std::int32_t mask = contents::Solid) const;
};

// Throws FormatError.
BspFile parse_bsp(std::span<const std::byte> data);

// Parses the entity lump text: a sequence of { "key" "value" ... } blocks.
std::vector<BspEntity> parse_bsp_entities(std::string_view text);

// Triangulated geometry for one face: positions, texture UVs (normalized by
// the texdata size), lightmap coordinates and displacement alpha.
// Displacements are tessellated.
struct BspFaceMesh {
    std::vector<Vec3> positions;
    std::vector<std::array<float, 2>> uvs;
    // In luxels, relative to the face's lightmap: 0..lightmap_size (inclusive).
    std::vector<std::array<float, 2>> lightmap_uvs;
    std::vector<float> alphas;
    std::vector<std::uint32_t> indices;
};

// Decodes one ColorRGBExp32 luxel (r, g, b, signed exponent) to linear RGB.
std::array<float, 3> decode_rgbe(const std::byte* luxel);
BspFaceMesh build_face_mesh(const BspFile& bsp, std::size_t face_index);

} // namespace openbl::formats
