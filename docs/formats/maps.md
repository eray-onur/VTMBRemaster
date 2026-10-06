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
| 6 | TexInfo | 72 | texture vectors [2][4], lightmap vectors [2][4], flags, texdata |
| 7 | Faces | **104** | see below |
| 8 | Lighting | 4 per luxel | `ColorRGBExp32` |
| 12 | Edges | 4 | two `u16` vertex indices |
| 13 | SurfEdges | 4 | signed edge index (negative = reversed) |
| 14 | Models | 48 | mins, maxs, origin, head node, first face, face count; model 0 is the world |
| 26 | DispInfo | 176 | start position, first vertex, first triangle, power, …, face (`u16` at 36) |
| 33 | DispVerts | 20 | direction, distance, alpha |
| 35 | Game lump | | `sprp` v4 (static props), `dprp` v2 (detail props), `dplt` (detail prop lighting) |
| 40 | Pakfile | zip | uncompressed (stored) zip of per-map files |
| 43, 44 | TexData strings | | string data and offset table |

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
