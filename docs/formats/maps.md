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
| 34 | Displacement lightmap sample positions | | not decoded |
| 33 | DispVerts | 20 | direction, distance, alpha |
| 35 | Game lump | | `sprp` v4 (static props), `dprp` v2 (detail props), `dplt` (detail prop lighting); see below |
| 36 | Leaf water data | 12 | `f32` surface z, `f32` minimum z, `i16` texinfo, `i16` |
| 37 | Primitives | 10 | `u16` type (always 1, triangle strip), `u16` first index, `u16` index count, `u16` first vertex, `u16` vertex count |
| 38 | Primitive vertices | 12 | extra points on a face's plane |
| 39 | Primitive indices | 2 | **absolute** positions in lump 38, within the primitive's vertex range |
| 40 | Pakfile | zip | uncompressed (stored) zip of per-map files |
| 41 | Clip portal vertices | 12 | outlines of area portals |
| 42 | Cubemaps | 16 | `i32` origin[3], `i32` size (0 = default). The textures are in the pakfile as `maps/<map>/c<x>_<y>_<z>`, referenced by materials' `$envmap` |
| 43, 44 | TexData strings | | string data and offset table |
| 48 | | 64 | all zero bytes; count unrelated to anything else |

Lumps 9 (occlusion), 11, 28 and 45 (overlays) are empty in every map. Decals are `infodecal` entities (4,609 in all), applied when the map loads.

Also decoded: world lights (15), nodes (5) and leafs (10); see below. Not yet decoded: visibility (4), leaf faces and brushes (16, 17), brushes (18, 19), areas and portals (20–25), physics collision (29), vertex normals (30, 31), cubemaps (42), detail props.

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

## Collision (lump 29)

A sequence of blocks `{i32 model, i32 data size, i32 text size, i32 solid count}`, followed by that many solids (`i32` size + data, the same format as `.phy` files; see [physics.md](physics.md)), then KeyValues text (`solid`, `staticsolid`, `fluid`, `materialtable`). A model of −1 ends the list. Every map has collision for model 0 (the world). Most solids use the `IVPS` layout. 165 large world solids use `MOPP`: their pieces are pairs of triangles stored back to back up to a shared point pool, and the tree offset points at compiled MOPP code, which is not needed to read the triangles. In all: 6,237 collision models, 1.93 million triangles.
