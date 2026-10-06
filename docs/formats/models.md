# Models (`.mdl` v2531 + `.vtx` v107)

A model is split across several files:

| File | Contents |
|---|---|
| `.mdl` | Header, bones, textures, skins, body parts, models, meshes, **and the vertex data** |
| `.dx80.vtx` | Triangle lists per mesh and level of detail (LOD), for DirectX 8+ hardware |
| `.dx7_2bone.vtx` | The same, regrouped for 2-bone skinning hardware |
| `.phy` | Collision and ragdoll data (not yet decoded) |

These are Troika's own versions: `.mdl` version **2531** and `.vtx` version **107**. The structure is related to Valve's 2003-era studio model format, but the sizes and fields differ. There is no `.vvd`: vertices live in the `.mdl`.

Implementation: `src/formats/mdl.cpp` and `src/formats/vtx.cpp`. The layouts below were checked against all 4,255 models and 8,509 `.vtx` files of the Steam release. A model of the cop renders correctly in its bind pose.

All integers are little-endian. "rel" means an offset relative to the start of the struct that contains it.

## `.mdl` header (0x1a8 bytes)

| Offset | Type | Field |
|---|---|---|
| 0x00 | `char[4]` | `"IDST"` |
| 0x04 | `i32` | version (2531) |
| 0x08 | `i32` | checksum (equals the `.vtx` checksum) |
| 0x0c | `char[128]` | name |
| 0x8c | `i32` | file length |
| 0xb4 | `f32[3]`, `f32[3]` | bounds min/max (see note below) |
| 0xe4 | `i32` | flags |
| 0xf0 | `i32, i32` | bone count, bone offset (always 0x1a8) |
| 0xf8 | `i32, i32` | bone controller count, offset |
| 0x100 | `i32, i32` | hitbox set count, offset |
| 0x108 – 0x120 | | animations and sequences (not yet decoded) |
| 0x124 | `i32, i32` | texture count, offset |
| 0x12c | `i32, i32` | texture-directory count, offset (to an array of absolute string offsets) |
| 0x134 | `i32, i32, i32` | skin reference count, skin family count, skin table offset (`i16[families][refs]`) |
| 0x140 | `i32, i32` | body part count, offset |
| 0x148 – 0x1a4 | | attachments, flexes, IK chains, pose parameters, … (not yet decoded) |

**Bounds**: for static props (one bone), the box at 0xb4 matches the vertex bounds exactly in most models (1,592 of 1,829 scenery props). For animated models it often does not contain the reference-pose vertices, so it probably describes a posed or movement hull. Use the vertices themselves for rendering bounds.

## Structures

**Bone** (160 bytes): `i32 name (rel)`, `i32 parent` (−1 for root; always lower than the bone's own index), `i32 controllers[6]`, `f32 position[3]` at 0x20, `f32 quaternion[4]` (x, y, z, w) at 0x2c, two `f32[3]` scales at 0x3c and 0x48, one `f32` at 0x54, `f32 pose_to_bone[3][4]` (inverse bind pose) at 0x58, 24 more bytes not yet decoded.

**Texture** (20 bytes): `i32 name (rel)`, then 16 bytes not yet decoded. A mesh's material is a skin reference. The skin table maps it to a texture, and the material is found at `materials/<texture dir>/<texture name>.vmt` for the first directory where the file exists.

**Body part** (16 bytes): `i32 name (rel)`, `i32 model count`, `i32 base`, `i32 model offset (rel)`.

**Model** (224 bytes):

| Offset | Type | Field |
|---|---|---|
| 0x00 | `char[128]` | name (source `.smd`) |
| 0x88 | `i32, i32` | mesh count, mesh offset (rel) |
| 0x90 | `i32, i32` | vertex count, vertex offset (rel) |
| 0x98 | `i32` | tangent offset (rel; `f32[4]` per vertex) |
| 0x9c | `i32` | **vertex format**: 0, 1 or 2 (see below) |
| 0xa0 | `f32[3]` | quantization origin |
| 0xac | `f32[3]` | quantization scale |

**Mesh** (60 bytes): `i32 material` (skin reference), `i32 model offset (rel, negative)`, `i32 vertex count`, `i32 vertex offset` (into the model's vertices; meshes are consecutive), then 44 bytes not yet decoded.

## Vertex formats

| Format | Models | Size | Layout |
|---|---|---|---|
| 0, float | 2,934 | 44 | `u8 weight[3]` (sum 255), `u8 ?`, `u16 bone[3]`, `u16 ?`, `f32 position[3]`, `f32 normal[3]`, `f32 uv[2]` |
| 1, 16-bit | 1,081 | 12 | `u16 position[3]`, `u16 packed normal`, `u16 uv[2]` |
| 2, 8-bit | 354 | 8 | `u8 position[3]`, `u8 normal index`, `u16 uv[2]` |

- **Format 1 positions**: `origin + value × scale`, so `origin` is the minimum corner and `scale` is extent ÷ 65535. UV is value ÷ 65535.
- **Format 2 positions**: `origin + scale × (value ÷ 127.5 − 1)`, so `origin` is the center and `scale` the half-extent. This reproduces the bounding box exactly. UV is value ÷ 65535; this is unverified.
- **Bone weights**: unused bone slots are 0, and bone indices fit the bone count. Byte 3 and bytes 10–11 are not understood yet. They are not a bone count.
- Quantized formats only occur in models whose vertices all use bone 0.

### Packed normals (not fully decoded)

**Format 1**: the 16-bit value is always a multiple of 12. It is evidently a byte offset into a table of 5,314 `float[3]` normals (indices 0–5313). What is known:

- The table is ordered by latitude in **66 rings** at polar angles θⱼ = j·π/65, with +z at index 0 and −z at index 5313. The z component matches cos θⱼ for 90% of measured vertices (the rest are noisy samples).
- Ring sizes are roughly 128·sin θ, rounded to multiples of 8. The equatorial rings hold 128 points, starting at azimuth 0 (+x).
- The azimuth phase of each ring differs systematically by latitude band and is not yet pinned down.

**Format 2**: byte 3 is an index from 0 to 201, apparently into a smaller table of directions.

Until both tables are recovered, quantized vertices report `has_normal = false` and carry the raw code in `packed_normal`. Renderers compute smooth normals from geometry instead.

To do: recover both tables.

## Animation

Verified on every model in the Steam release: 11,039 animations and 10,739 sequences. All 1,368,576 sampled bone rotations are unit quaternions, and all 13,305 animation channels checked in the male stance and conversation files decode to exactly their frame count and byte length. The cop renders correctly in both an "arms crossed" stance and a "hands behind back" stance.

### Header fields

| Offset | Fields |
|---|---|
| 0x108 | animation count, offset (72-byte animation descriptions) |
| 0x110 | sequence count, offset (764-byte sequences) |
| 0x11c, 0x120 | sequence group count, offset (one `default` group in practice) |
| 0x194, 0x198 | **include model** count, offset (116-byte entries; name at +0, relative) |

Characters keep almost no animation of their own. The cop, for example, has only `ragdoll`, and includes `models/character/shared/male/npc_allsequences.mdl`. That file in turn includes `stances.mdl`, `conversations.mdl` and `allsequences.mdl`, which hold the shared animations. Animation bones are matched to model bones by name; across the male skeleton the 53 shared bones have identical names, parents and bind poses.

### Structures

**Animation description** (72 bytes): `i32 name (rel)` (e.g. `@Stance_Anger_Idle_1`), `f32 fps`, `i32 flags`, `i32 frame count`, `i32 movement count`, `i32 movement offset`, `f32 bounds[6]`, `i32 data offset (rel)` at +48, then 20 bytes not yet decoded.

**Sequence** (764 bytes): `i32 label (rel)`, `i32 activity name (rel)`, `i32 flags` (bit 0 = looping), `i32 activity`, `i32 activity weight`, `i32 event count`, `i32 event offset`, `f32 bounds[6]`, `i32 blend count` at +52, `i16 animation index[]` at +56 (one per blend), group sizes at +572 and +576. The rest is not yet decoded.

**Animation data:** one 32-byte record per bone, `f32` (always 1.0) and then seven `u32` channel offsets relative to the record: position x, y, z, rotation x, y, z, w. An offset of 0 means the channel is not stored. Each channel is run-length encoded in 16-bit entries. A header entry holds `valid` and `total` bytes, in that order, followed by `valid` signed values that cover `total` frames; frames past `valid` repeat the last value. Runs follow one another until the frame count is reached.

- **Rotation** components are **absolute**: `value × bone.rotation_scale[c]` (bone +0x48…+0x54, a separate scale per component, chosen to maximise precision).
- **Position** components are **offsets from the bind position**: `bone.position[c] + value × bone.position_scale[c]` (bone +0x3c, 1/256 on characters).
- A channel that is not stored keeps the bind value.

**Bone flags** (bone +0x88). Bit `0x2` is set on every `BipNN Spine1` bone (525 bones in all) and means **the rotation is relative to the top of the hierarchy (`Bip01`), not to the parent**. The position still chains through the parent. Treating it as parent-relative bends characters 120° at the waist. Root-relative gives a median of 7° from bind across all 67 male stances.

**Shipped data issues:** 8 sequences reference animations that the file does not contain. These are models compiled with their animations stripped (`throwtaxi`, `woodbox/lid`, `drknobantique*_static`). OpenBL marks them invalid (`animation = -1`).

**Not yet decoded:** extra blends of multi-blend sequences (aiming, directional movement), movement (root motion), events, IK, and the root bone's facing convention. Animation roots are stored about 90° off the bind root about the vertical axis.

## `.vtx` layout

| Struct | Size | Fields |
|---|---|---|
| Header | 36 | `i32 version (107)`, `i32 vertex cache size`, `u16 max bones/strip`, `u16 max bones/tri`, `i32 max bones/vertex`, `i32 checksum`, `i32 LOD count`, `i32 material replacement offset`, `i32 body part count`, `i32 body part offset` |
| Body part | 8 | `i32 model count`, `i32 model offset (rel)` |
| Model | 8 | `i32 LOD count`, `i32 LOD offset (rel)` |
| LOD | 12 | `i32 mesh count`, `i32 mesh offset (rel)`, `f32 switch point` |
| Mesh | 8 | **`u16 strip group count`, `u16 flags`**, `i32 strip group offset (rel)` |
| Strip group | 20 | `u16 vertex count`, `u16 index count`, `u16 strip count`, `u8 flags`, `u8 ?`, `i32 vertex offset`, `i32 index offset`, `i32 strip offset` (all rel) |
| Strip | 16 | `u16 index count`, `u16 index offset`, `u16 vertex count`, `u16 vertex offset`, `u8 flags`, `u8 ?`, `u16 bone state change count`, `i32 bone state change offset` |

- **Vertex records**: if strip-group flag bit `0x10` is set, each record is a bare `u16` reference to a mesh vertex (static props). Otherwise records are 12 bytes, and the reference is the **last** `u16`; the preceding bytes hold bone-palette data for hardware skinning.
- **Indices** are `u16` triangle lists into the group's vertex records. Strips split them into contiguous ranges, which also partition the vertices.
- The mesh structure always matches the `.mdl` exactly: the same number of body parts, models and meshes in every LOD.
