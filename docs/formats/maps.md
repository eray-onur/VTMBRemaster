# Maps (`.bsp`, VBSP version 17)

Bloodlines maps use an early version (17) of Valve's BSP format. Most lumps use Valve's publicly documented layouts. The main exception is faces: Troika enlarged them to 104 bytes.

Implementation: `src/formats/bsp.cpp` (lumps, entities, face meshes) and `src/formats/zip.cpp` (pakfile). Verified against all 101 maps of the Steam release: 514,455 faces, 1,588,318 triangles and 24,409 static props.

## Header

`"VBSP"`, `i32 version` (17), then 64 lump entries of `{i32 offset, i32 length, i32 version, i32 fourCC}`, then `i32 map revision`.

## Lumps used

| # | Lump | Record | Notes |
|---|---|---|---|
| 0 | Entities | text | `{ "key" "value" … }` blocks; see below |
| 1 | Planes | 20 | normal, distance, type |
| 2 | TexData | 32 | reflectivity, name id, width, height, view width, view height |
| 3 | Vertices | 12 | |
| 4 | Visibility | | see below |
| 5 | Nodes | 32 | plane, children (≥ 0 node, < 0 leaf −(i + 1)), … |
| 6 | TexInfo | 72 | texture vectors [2][4], lightmap vectors [2][4], flags, texdata |
| 7 | Faces | **104** | see below |
| 8 | Lighting | 4 per luxel | `ColorRGBExp32` |
| 12 | Edges | 4 | two `u16` vertex indices |
| 13 | SurfEdges | 4 | signed edge index (negative = reversed) |
| 10 | Leafs | 32 | see below |
| 14 | Models | 48 | mins, maxs, origin, head node, first face, face count; model 0 is the world |
| 15 | World lights | 88 | origin, intensity, normal, cluster, type, style, cone, attenuation, flags, texinfo, owner |
| 16 | Leaf faces | 2 | face index |
| 17 | Leaf brushes | 2 | brush index |
| 18 | Brushes | 12 | first side, side count, contents (`0x1` solid, `0x8` grate, `0x8000000` detail, `0x10000000` translucent, …) |
| 19 | Brush sides | 8 | `u16` plane, `i16` texinfo, `i16` dispinfo, `i16` bevel (1 for compiler-added bevel planes) |
| 20 | Areas | 8 | portal count, first portal |
| 21 | Area portals | 12 | `u16` portal key, `u16` other area, `u16` first clip vertex, `u16` clip vertex count, `i32` plane |
| 22–25 | Portals, clusters, portal vertices, cluster portals | 16, 8, 2, 2 | compiler output for visibility; ranges consistent; not used at runtime |
| 26 | DispInfo | 176 | start position, first vertex, first triangle, power, …, face (`u16` at 36) |
| 27 | Original faces | 104 | faces before splitting (same record as faces; `original face` at +96 indexes this) |
| 29 | Collision | | see below |
| 30 | Vertex normals | 12 | unit vectors (one map has a few that are not) |
| 31 | Vertex normal indices | 2 | one per face corner, in face order: the faces' surfedges concatenated |
| 32 | Displacement lightmap alphas | 1 | one byte per displacement lightmap luxel |
| 34 | Displacement lightmap sample positions | 4–5 per luxel | for each luxel of each displacement face, in face order: the displacement triangle it was sampled on (one byte, or 255 plus a second byte), then three barycentric weights (bytes summing to 253–255). The lump decodes exactly in all 44 maps that have it (1,087,966 samples). |
| 33 | DispVerts | 20 | direction, distance, alpha |
| 35 | Game lump | | `sprp` v4 (static props), `dprp` v2 (detail props), `dplt` (detail prop lighting); see below |
| 36 | Leaf water data | 12 | `f32` surface z, `f32` minimum z, `i16` texinfo, `i16` |
| 37 | Primitives | 10 | `u16` type (always 1, triangle strip), `u16` first index, `u16` index count, `u16` first vertex, `u16` vertex count |
| 38 | Primitive vertices | 12 | extra points on a face's plane |
| 39 | Primitive indices | 2 | **absolute** positions in lump 38, within the primitive's vertex range |
| 40 | Pakfile | zip | uncompressed (stored) zip of per-map files |
| 41 | Clip portal vertices | 12 | outlines of area portals |
| 42 | Cubemaps | 16 | `i32` origin[3], `i32` size (0 = default). The textures are in the pakfile as `materials/maps/<map>/c<x>_<y>_<z>` (plus `cubemapdefault`), referenced by per-cubemap `patch` copies of materials; see [RENDERING.md](../RENDERING.md). `openbl-cli bsp pak <map>` lists a pakfile, `bsp pak-cat <map> <path>` prints a file |
| 43, 44 | TexData strings | | string data and offset table |
| 48 | | 64 | all zero bytes; count unrelated to anything else |

Lumps 9 (occlusion), 11, 28 and 45 (overlays) are empty in every map. Decals are `infodecal` entities (4,609 in all), applied when the map loads.

Also decoded: world lights (15), nodes (5), leafs (10), visibility (4), leaf faces and brushes (16, 17), brushes (18, 19), areas and portals (20–25), physics collision (29), vertex normals (30, 31), cubemaps (42) and the detail props; see below.

## Faces (104 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | `u32[8]` | average light colour per style (`ColorRGBExp32`) |
| 32 | `u16` | plane |
| 34 | `u8, u8` | side, on node |
| 36 | `i32` | first surfedge |
| 40 | `u16` | surfedge count |
| 42 | `u16` | texinfo |
| 44 | `i16` | dispinfo (−1 = none) |
| 46 | `i16` | fog volume |
| 48 | `u8[8]` | lightmap styles (255 = unused): **8 instead of Valve's 4** |
| 56 | 16 bytes | always zero in the shipped maps |
| 72 | `i32` | lighting lump byte offset |
| 76 | `f32` | area |
| 80 | `i32[2]` | lightmap mins, in luxels |
| 88 | `i32[2]` | lightmap size, in luxels minus one |
| 96 | `i32` | original face |
| 100 | `u16, u16` | primitive count, first primitive |

A face's lightmap takes `(w+1)·(h+1)·4` bytes per used style. All 392,965 lit faces' lightmaps fit inside the lighting lump, and consecutive faces pack their lightmaps tightly. Every face's edge loop is closed.

## Displacements

A displacement replaces a quad face with a `(2^power + 1)²` grid of vertices. Powers 2, 3 and 4 occur. To build the grid, rotate the face's corners so that the one nearest `start_position` comes first. Interpolate between the corners, then move each grid point by `direction × distance`. Texture coordinates come from the undisplaced surface. Every displacement's face points back to that displacement.

## Static props (`sprp` v4, 56 bytes each)

The lump holds a model-name dictionary (`char[128]` each), a leaf list (`u16`), then the props: origin, angles (pitch, yaw, roll), model index, first leaf, leaf count, solid, flags, skin, fade min, fade max, lighting origin. All 24,409 props reference models that exist.

Flags: bit 0 (`0x1`) marks a prop that fades out between fade min and fade max (7,920 props, exactly those with a fade distance; see [RENDERING.md](../RENDERING.md#static-prop-fades)); bit 1 (`0x2`) says to use the lighting origin. Bit 4 (`0x10`, 9,259 props) is not read by OpenBL yet.

## Materials and the pakfile

TexData names are relative to `materials/`. About a third (3,831) are per-map patched materials, such as `maps/sm_pier_1/…`, that live in the pakfile and not in the game archives. Only 2 of 11,184 names resolve nowhere. All pakfile entries are stored uncompressed. The zip reader also handles deflate.

## Lightmaps with bump lighting

Faces whose texinfo has `SURF_BUMPLIGHT` (`0x800`) store **four** lightmaps per style: the flat one, then three for normal-mapped lighting. Every other face stores one per style. With this rule, faces in the lighting lump are packed with no gaps; this was checked on `ch_lotus_1`, where 279 bump-lit faces have gaps of exactly 3× their size. Luxel exponents range from about −64 to +7; very negative exponents are simply dark luxels.

## Light styles

A face stores one lightmap per used style, in the order of its `styles` slots; bump-lit faces store four per style, of which the first is the flat one. Style 0 is always on. Styles 1–12 are the classic animated patterns. Styles 32 and up are switchable lights: the compiler writes the assigned style into each light entity's `style` key, next to its `targetname`, and spawnflag 1 means the light starts off. In the Steam release, `sm_pier_1` has `houselights` (style 32, starting on), and `ch_lotus_1` has `ovenlight` (style 32, starting off) plus animated styles 2, 6 and 10. Faces use at most 4 of their 8 style slots.

## World lights (lump 15, 88 bytes)

Valve's `dworldlight_t` layout: origin, intensity, normal, cluster, type, style, `stopdot`, `stopdot2`, exponent, radius, constant / linear / quadratic attenuation, flags, texinfo, owner. Types: 0 emissive surface, 1 point, 2 spot, 3 sky, 5 sky ambient. Point and spot lights use pure inverse-square falloff (quadratic = 1), sometimes with a radius cutoff. Emissive-surface lights store all three attenuation terms as zero; they fall off with the inverse square of the distance too.

**Units:** intensity is in the same linear units as the lightmaps. On floors directly below point lights, `intensity · cosθ / d²` matches the baked lightmap with a median ratio of 1.02 on `sm_pier_1` and 0.92 on `hw_hub_1`. The ratio is lower indoors, where geometry blocks some lights. So props lit from world lights match the world's brightness under the same overbright scale.

## BSP tree

Nodes (32 bytes: plane, two children, bounds, faces, area) and leafs (32 bytes: contents, cluster, area/flags, bounds, leaf faces, leaf brushes, water id) use Valve's layouts. In every map the world model's head node is node 0, and every node references valid planes and children. Solid leafs (`contents & 1`) have cluster −1. `BspFile::segment_blocked` traces a line through this tree for light visibility.

## Skybox

The worldspawn `skyname` (`santamonica`, `holly`, `la`, `pier`, `chinatown`, `warehouse`, `hav` or `moon`) names six `UnlitGeneric` materials, `materials/skybox/<name>{rt,lf,bk,ft,up,dn}.vmt`. Every map has all six. The faces map onto the cube as follows; each entry gives the direction of increasing u and of decreasing v, as seen from inside:

| Face | Direction | u → | v ↑ |
|---|---|---|---|
| rt | +X | −Y | +Z |
| lf | −X | +Y | +Z |
| bk | +Y | +X | +Z |
| ft | −Y | −X | +Z |
| up | +Z | −Y | −X |
| dn | −Z | −Y | +X |

This was verified by colour continuity across all 12 cube edges of the `santamonica` and `holly` skyboxes. Mean edge difference is 3.4, against 35.9 for the worst alternative mapping. The `dn` rotation cannot be confirmed this way, because the face is a uniform colour.

## Entities

The entity lump is plain text without the KeyValues extras: no comments and no nesting. One Bloodlines-specific detail matters: values containing Python code escape quotes as `\"`, for example `"python_script" "IsDead(\"Mandarin\")"` in `ch_fulab_1`. The parser turns `\"` into `"` and keeps all other backslashes literal, because paths use them as separators.

## Leafs (32 bytes)

| Offset | Field |
|---|---|
| 0 | `i32` contents |
| 4 | `i16` cluster (−1 = outside the visibility data) |
| 6 | `u16`: bits 0–8 area, bits 9–15 flags |
| 8 | `i16` mins[3], `i16` maxs[3] |
| 20 | `u16` first leaf face, `u16` leaf face count |
| 24 | `u16` first leaf brush, `u16` leaf brush count |
| 28 | `i16` leaf water data index, −1 for none |
| 30 | padding |

All 193,957 leafs of the Steam release have valid ranges, boxes and areas.

## Visibility (lump 4)

`i32` cluster count *n*, then *n* pairs of `i32` byte offsets into the lump: the row of clusters visible from each cluster (PVS) and the row it can hear (PAS). Each row is *n* bits, run-length compressed: a zero byte is followed by a count of zero bytes; any other byte is literal. All 52,747 rows in the release decompress to exactly ⌈n/8⌉ bytes, every cluster sees itself, and the sets are symmetric. On average a cluster sees 27% of the map.

## Primitives

516 faces carry primitives: triangle strips over extra vertices that close T-junction cracks between neighbouring faces. A strip's winding alternates, so each triangle is oriented to match the face polygon. The strips cover the polygon to within 1% for 512 faces. Four lose a 1.5–4% sliver, apparently dropped by the compiler.

## Detail props (`dprp` v2, `dplt`)

`dprp`: `i32` model count, then 128-byte model names, then `i32` prop count and 40-byte props. Unlike later versions there is no sprite dictionary.

| Offset | Field |
|---|---|
| 0 | `f32` origin[3] |
| 12 | `f32` angles[3] |
| 24 | `u16` model, `u16` leaf |
| 28 | lighting (`ColorRGBExp32`) |
| 32 | `u32` first light style (0xFFFFFFFF = none) |
| 36 | `u8` light style count |
| 37 | 3 bytes, often 0xCD (uninitialised memory in the compiler) |

`dplt`: `i32` count, then 5-byte `{ColorRGBExp32, u8 style}`. 134,490 detail props in all, mostly trash and grass models.

## Decals (`infodecal`)

There are 4,609 `infodecal` entities across 80 maps, each with only a `texture` (a material under `materials/`) and an `origin`. They use 375 materials, all present except one: 337 `LightmappedGeneric`, 22 `UnlitGeneric`, 9 `DecalModulate` and 6 `VertexLitGeneric`. Most set `$decal 1`, `$translucent 1` and `$decalscale`. Eleven name a `$modelmaterial` variant for use on models.

`project_decal` places them as follows:

- **Size:** the base texture's size × `$decalscale`, in world units.
- **Faces:** every visible planar face of the world and of brush entities, at their origins, that has the origin in front of it and within 4 units. Tool brushes such as triggers and clips take no decals.
- **Behind the surface:** if no face qualifies, faces with the origin up to 8 units *behind* them are used instead. Many shipped decals sit just inside their wall: Santa Monica's Asylum posters 1 to 2 units, tunnel lights 5, a light switch 8.
- **Displacements:** failing both, the decal goes on every displacement whose bounds overlap the box of half the decal's larger side around the origin, as the original does, projected from the nearest of their triangles within that distance (`project_mesh_decal`, `openbl/formats/mesh_decal.hpp`).
- **Clipping:** the decal rectangle is clipped to each face, so a decal in a corner wraps onto both walls.
- **Orientation:** the decal's up axis follows world +z on walls and world +y on floors and ceilings. This is our choice, not taken from the engine. Parking-bay stencils in `la_parkinggarage_1` read correctly from the lane on both rows, and Santa Monica's posters read correctly, but other orientations are unconfirmed until there are screenshots of the original.

This places 4,520 of 4,609 decals on the world (98.1%; 4,401 with front faces alone) at a test size of 32 × 32; at their real sizes the displacement rule places a few large sprays more (`hw_hub_1`: 3). **Props:** `PropRenderer` projects the rest onto the nearest prop surface within 8 units, in the prop's model space, using the decal's `$modelmaterial` variant if it has one; this finds only a handful (one in `sp_tutorial_1`).

**The original's rule**, read from Bloodlines' `engine.dll` (`R_DecalNode` at `0x20085510`, the leaf pass at `0x20084f70`, `R_DecalCreate` at `0x20084af0`, found through its `psurface NULL in R_DecalCreate!` message): a face lying on a BSP node takes the decal when the origin is within 4 units of the node's plane, in front or behind (`DECAL_DISTANCE`, the constants 4.0 and −4.0); a detail face in a leaf, when |distance to its plane| < 4; a displacement, when its bounds overlap the origin ± the decal's half size. So the decals left over (about 85, mostly graffiti sprays and posters 20 to 64 units in front of walls, such as `sm_hub_1`'s five sprays above the alley dumpsters and `la_expipe_1`'s upstairs posters, 64 units in front of the wall above copies placed correctly downstairs) are not drawn in the original either. OpenBL's 8-unit fallback behind a surface goes beyond the original: decals 4 to 8 units behind (a light switch, tunnel lights) are drawn in OpenBL only. They look intended, so OpenBL keeps them.

**Rendering** (`WorldRenderer`):

- Decals are drawn after the opaque world, lit by the lightmap of the face under them, alpha-blended and lifted 0.1 units off the face.
- `DecalModulate` multiplies what is behind it by 2 × the texture in gamma space. In linear space that is `dst × 2^2.2 × base`, done as a `2 × src × dst` blend with the shader outputting `2^1.2 × base`.

## Brush collision

`BspFile::brush_hull(brush)` turns a brush into a convex polyhedron: one polygon per side plane, clipped by all the other sides. `point_in_brush` tests a point against the same planes.

**Bevel sides are left out.** The compiler adds bevel planes to speed up box traces, and on a correct brush they never cut the solid. In 9 brushes in the release, though, some bevels contradict the real sides by 3 to 254 units and would leave the hull empty. Examples: `la_hub_1` brush 7215, and five brushes in `la_abandoned_building_1`. Without bevels, all 121,350 solid brushes give closed hulls.

**Check against the compiled collision.** Of the 1,496,635 vertices in the world's physics hulls (lump 29, model 0):

- 1,232,961 lie on or inside a brush, within 1 unit;
- 263,652 lie on displacement surfaces;
- 22 are unexplained.

So brushes plus displacements reproduce the world's collision. The test is in `tests/test_bsp.cpp`.

## Collision (lump 29)

A sequence of blocks `{i32 model, i32 data size, i32 text size, i32 solid count}`, followed by that many solids (`i32` size + data, the same format as `.phy` files; see [physics.md](physics.md)), then KeyValues text (`solid`, `staticsolid`, `fluid`, `materialtable`). A model of −1 ends the list. Every map has collision for model 0 (the world). Most solids use the `IVPS` layout. 165 large world solids use `MOPP`: their pieces are pairs of triangles stored back to back up to a shared point pool, and the tree offset points at compiled MOPP code, which is not needed to read the triangles. In all: 6,237 collision models, 1.93 million triangles.
