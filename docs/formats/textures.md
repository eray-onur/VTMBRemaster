# Textures (`.tth` + `.ttz`)

Bloodlines stores every texture as a pair of files:

- **`.tth`** (Troika texture header): a small header, followed by the start of a standard **VTF 7.1** file. That start covers the VTF header, the low-resolution thumbnail and the smallest mip levels.
- **`.ttz`**: the rest of the same VTF file (the larger mips) as a single zlib stream.

Joining the `.tth` VTF prefix with the decompressed `.ttz` gives back a complete, ordinary VTF file. This lets the engine load small mips cheaply and stream in the large ones.

Implementation: `src/formats/tth.cpp` (assembly) and `src/formats/vtf.cpp` (VTF parsing).

## `.tth` layout

All integers are little-endian.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | `"TTH\0"` |
| 4 | `u8` | version major (always 1) |
| 5 | `u8` | version minor (always 0) |
| 6 | `u8` | `mip_count`, equal to the VTF's mip count |
| 7 | `u8` | `resident_mips`: how many of the smallest mips are stored in the `.tth` |
| 8 | `u32` | `vtf_prefix_size`: bytes of VTF data stored in the `.tth` |
| 12 | `{u32 vtf_end, u32 ttz_end}[mip_count + 1]` | mip range table |
| 12 + 8·(mip_count+1) | `byte[vtf_prefix_size]` | start of the VTF file |

The range table describes the VTF in storage order. Entry 0 ends the VTF header plus thumbnail. Entry *i* ends the *i*-th smallest mip, including all of its frames and faces.

- `vtf_end` is the end offset of that range in the assembled VTF.
- `ttz_end` is the end offset of that range in the `.ttz` stream. It is 0 for ranges held in the `.tth`, which is exactly entries 0 to `resident_mips`.
- `vtf_end` of entry `resident_mips` equals `vtf_prefix_size`.

A few `.tth` files carry extra bytes after the VTF prefix: 16 to 192 bytes, all `0xCD`. That is the MSVC debug heap's fill pattern for uninitialised memory, written out by the original tools. These bytes are ignored.

## `.ttz` layout

A single zlib stream (RFC 1950, `78 DA` header) holding VTF bytes `vtf_prefix_size` to `vtf_end` of the last entry. A `Z_SYNC_FLUSH` follows each mip, so each `ttz_end` is a byte-aligned point in the stream, and mips could be decompressed one at a time.

Some `.ttz` files have extra bytes after the last `ttz_end` (243 in the Steam release). They are ignored.

## Embedded VTF

Every texture in the Steam release is VTF version 7.1 with a 64-byte header:

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | `"VTF\0"` |
| 4 | `u32[2]` | version (7, 1) |
| 12 | `u32` | header size (64) |
| 16 | `u16, u16` | width, height |
| 20 | `u32` | flags (`0x4000` = environment map) |
| 24 | `u16` | frame count |
| 26 | `u16` | first frame |
| 28 | — | 4 bytes padding |
| 32 | `f32[3]` | reflectivity |
| 44 | — | 4 bytes padding |
| 48 | `f32` | bump scale |
| 52 | `i32` | image format |
| 56 | `u8` | mip count |
| 57 | `i32` | thumbnail format (−1 = no thumbnail) |
| 61 | `u8, u8` | thumbnail width, height |

Image data starts at the header size. First comes the thumbnail, then each mip **from smallest to largest**. Within a mip, frames are stored one after another, and within each frame its faces. Every mip's size was checked against the `.tth` range tables for all 10,909 textures, with no mismatches:

- **Cubemaps** (environment-map flag) always have **7 faces**: 6 cube faces plus a sphere map. This holds whether the first frame is 0 or `0xFFFF`.
- Block-compressed formats round each dimension up to whole 4×4 blocks, so even a 1×1 mip takes one full block.

Image formats used in the Steam release:

| Format | Id | Textures |
|---|---|---|
| DXT1 | 13 | 7,522 |
| DXT5 | 15 | 3,009 |
| RGBA8888 | 0 | 242 |
| BGR888 | 3 | 77 |
| BGRA8888 | 12 | 48 |
| UVWQ8888 | 23 | 11 |

Thumbnails are DXT1, or absent in 261 textures. 42 textures are animated (more than one frame), and 42 are cubemaps.

## Damaged textures in the Steam release

| Texture | Problem |
|---|---|
| `materials/models/character/npc/unique/santa_monica/danielle/heather_hair_5` | `.ttz` missing |
| `materials/models/scenery/furniture/chinesesigns/chinesesigns_test` | `.ttz` missing |
| `materials/models/character/eyes/demen` | `.ttz` decompresses to 734 of 1,024 bytes and has a bad checksum |
| `materials/models/character/eyes/whiteglowing` | `.ttz` decompresses to 32 of 64 bytes and has a bad checksum |
| `materials/models/character/pc/male/tremere/armor0/trmrm0footbottom` | `.ttz` decompresses to 65 bytes instead of 64 and has a bad checksum |

OpenBL keeps every mip that is completely present and reports the texture as damaged. Because mips are stored smallest first, missing data only ever removes the largest mips.

To do: check what the original engine shows for these textures, and match it.
