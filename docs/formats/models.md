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
| 0x180, 0x184 | **pose parameter** count, offset (20-byte entries) |
| 0x194, 0x198 | **include model** count, offset (116-byte entries; name at +0, relative) |

Characters keep almost no animation of their own. The cop, for example, has only `ragdoll`, and includes `models/character/shared/male/npc_allsequences.mdl`. That file in turn includes `stances.mdl`, `conversations.mdl` and `allsequences.mdl`, which hold the shared animations. Animation bones are matched to model bones by name; across the male skeleton the 53 shared bones have identical names, parents and bind poses.

### Structures

**Animation description** (72 bytes): `i32 name (rel)` (e.g. `@Stance_Anger_Idle_1`), `f32 fps`, `i32 flags`, `i32 frame count`, `i32 movement count`, `i32 movement offset (rel)`, `f32 bounds[6]`, `i32 data offset (rel)` at +48. The last 20 bytes are zero apart from two animations.

**Movement piece** (44 bytes; root motion): `i32 end frame`, `i32 flags`, `f32 start speed`, `f32 end speed`, `f32 yaw`, `f32 direction[3]`, `f32 displacement[3]`. A piece runs from the previous piece's end frame (or 0) to its own. End frames always increase, and the last piece ends on the last frame (all 22,873 pieces in 1,362 animations). The flags name the axes that move, and match the displacement without exception: `0x40` x, `0x80` y, `0x100` z, `0x800` yaw. `0x1000` is set on most pieces; its meaning is unknown.

**Pose parameter** (20 bytes): `i32 name (rel)`, `i32 flags`, `f32 start`, `f32 end`, `f32 wrap` (360 for parameters that loop, else 0). The shipped models use `move_yaw` and `hit_yaw` (−180…180, looping) and `aim_yaw`/`aim_pitch` (−45…45).

**Sequence** (764 bytes):

| Offset | Field |
|---|---|
| +0 | `i32` label (rel) |
| +4 | `i32` activity name (rel) |
| +8 | `i32` flags (bit 0 = looping) |
| +12 | `i32` activity, −1 in every file (resolved by name at load time) |
| +16 | `i32` activity weight |
| +20, +24 | event count, offset (rel) |
| +28 | `f32` bounds[6] |
| +52 | `i32` blend count (= grid[0] × grid[1]) |
| +56 | `i16` animation index[16][16]: the blend grid; entry [i][j] at +56 + 2·(16i + j) |
| +572, +576 | `i32` grid size per axis (1, 2, 3, 5 or 9 × 1 or 3) |
| +580, +584 | `i32` pose parameter per axis, −1 if none |
| +588, +592 | `f32` parameter value at the first grid column/row |
| +596, +600 | `f32` parameter value at the last |
| +612, +616 | `f32` fade-in and fade-out time (0.2 s for most) |
| +620 | `f32` a third time, usually equal to the others; meaning unknown |
| +660, +664 | count and offset (rel) of a small float block; meaning unknown |
| +672 | `f32` a second bounding box |
| +700, +704 | count, offset (rel) of **melee sweep points** (24 bytes: two points) |
| +708, +712 | count, offset (rel) of **melee hits** (188 bytes) |
| +716, +720 | `f32` pair set with the melee data (e.g. 120, 213); FLT_MIN/FLT_MAX otherwise |
| +752…+760 | `f32` values, mostly 0, 1, 1; meaning unknown |

Blend grids step evenly from the start to the end value. Andrei's `run` blends `run_180`, `run_225` … `run_135`, `run_180` across `move_yaw` −180…180.

**Event** (76 bytes): `f32 cycle` (0–1 through the sequence; all 1,726 events are in range), `i32 event id`, `i32 type` (always 0), `char options[64]`. The ids are game codes. The most common, with the options they carry:

| Id | Uses | Options seen | Probably |
|---|---|---|---|
| 4006, 4007 | 272, 104 | — | creature footsteps or attacks (rat) |
| 2050–2053 | 567 | — | melee attack phases |
| 3031 | 105 | — | Ming Xiao attack |
| 5001, 5003 | 146 | `4` | weapon fire or reload |
| 5118, 5116, 5117, 5120, 5112, 5115 | 111 | `Root;d_animalism_bat_trail_emitter`, `BlowSmoke_Emitter` | start a particle emitter, optionally at an attachment |
| 4100–4102 | 65 | `models/items/Bum_Bottle/Bum_Bottle` | attach or detach a prop model |
| 4050, 4051 | 80 | `Stealth_Kill` | stealth kill |
| 4150–4154 | 16 | `cel_ring`, `Cough` | phone and character sounds |
| 1003, 1005 | 27 | `MX_CHARGE_ROAR` | sounds |
| 6001, 6002, 6013 | 57 | `0 6` | view-model weapon events |

**Melee hit** (188 bytes): `f32 start cycle`, `f32 end cycle`, `i32 bone` (usually a hand), `f32 value` (often 15, probably damage or reach), five `f32` (unknown), then `-1` padding, then four 16-byte **reaction slots** at +120, +136, +152 and +168. Each slot starts with a record-relative activity name such as `ACT_KNOCKBACK_NORMAL_HIGH_RIGHT`, probably one per direction the victim can face. There are 1,460 hits in 528 sequences.

**Animation data:** one 32-byte record per bone, `f32` (always 1.0) and then seven `u32` channel offsets relative to the record: position x, y, z, rotation x, y, z, w. An offset of 0 means the channel is not stored. Each channel is run-length encoded in 16-bit entries. A header entry holds `valid` and `total` bytes, in that order, followed by `valid` signed values that cover `total` frames; frames past `valid` repeat the last value. Runs follow one another until the frame count is reached.

- **Rotation** components are **absolute**: `value × bone.rotation_scale[c]` (bone +0x48…+0x54, a separate scale per component, chosen to maximise precision).
- **Position** components are **offsets from the bind position**: `bone.position[c] + value × bone.position_scale[c]` (bone +0x3c, 1/256 on characters).
- A channel that is not stored keeps the bind value.

**Bone flags** (bone +0x88). Bit `0x2` is set on every `BipNN Spine1` bone (525 bones in all) and means **the rotation is relative to the top of the hierarchy (`Bip01`), not to the parent**. The position still chains through the parent. Treating it as parent-relative bends characters 120° at the waist. Root-relative gives a median of 7° from bind across all 67 male stances.

**Shipped data issues:** 8 sequences reference animations that the file does not contain. These are models compiled with their animations stripped (`throwtaxi`, `woodbox/lid`, `drknobantique*_static`). OpenBL marks them invalid (`animation = -1`).

**Shipped data issues (blends):** the crooked cop's three walkie-talkie sequences are 2×1 grids of two copies of the same animation, with no pose parameter.

**Not yet decoded:** the root bone's facing convention (animation roots are stored about 90° off the bind root about the vertical axis), the +660 float block, and the meaning of the unknown melee fields. No IK data has been found; if the format has any, it is in one of the unknown fields.

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
