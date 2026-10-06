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

Not yet decoded: nodes, leafs and visibility (4, 5, 10, 16, 17), brushes (18, 19), areas and portals (20–25), world lights (15), physics collision (29), vertex normals (30, 31), cubemaps (42), detail props.

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
