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
| 0xf8 | `i32, i32` | bone controller count (0 in every file), offset |
| 0x100 | `i32, i32` | hitbox set count, offset (12-byte sets) |
| 0x108 – 0x120 | | animations and sequences (see Animation) |
| 0x124 | `i32, i32` | texture count, offset |
| 0x12c | `i32, i32` | texture-directory count, offset (to an array of absolute string offsets) |
| 0x134 | `i32, i32, i32` | skin reference count, skin family count, skin table offset (`i16[families][refs]`) |
| 0x140 | `i32, i32` | body part count, offset |
| 0x148 | `i32, i32` | attachment count, offset (60 bytes each) |
| 0x150 | `i32, i32` | always 0 |
| 0x158 | `i32, i32` | flex count, offset (4 bytes each: name, rel) |
| 0x160 | `i32, i32` | flex controller count, offset (20 bytes each) |
| 0x168 | `i32, i32` | flex rule count, offset (12 bytes each, followed by their ops) |
| 0x170 | `i32, i32` | IK chain count, offset (16 bytes each, followed by their links) |
| 0x178 | `i32, i32` | mouth count, offset (20 bytes each) |
| 0x180 | `i32, i32` | pose parameter count, offset (see Animation) |
| 0x188 | `i32` | surface property: absolute offset of a string (`metal`, `wood`, `flesh`; empty on 2,412 models) |
| 0x18c | `i32, i32` | count and offset of unknown data (85 models) |
| 0x194 | `i32, i32` | include model count, offset (see Animation) |
| 0x1a4 | `i32` | always 1 |

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

**Mesh** (60 bytes): `i32 material` (skin reference), `i32 model offset (rel, negative)`, `i32 vertex count`, `i32 vertex offset` (into the model's vertices; meshes are consecutive), `i32 flex count`, `i32 flex offset (rel)`, `i32 material type` (1 on 496 meshes), `i32 material parameter`, 16 zero bytes, then three offsets at +48, +52 and +56 set on 74 meshes. Those meshes belong to hair, fur and cloth (prostitute, stripper, wolf form, `scenery/structural/temple/panel_cloth`), so this is probably cloth-simulation data. The offsets are relative to the model and point at three consecutive arrays whose spacing scales with the vertex count (1 and 2, 6 and 12, or 7 and 14 bytes per vertex). Some hold increasing 16-bit vertex indices, others floats. Not decoded further.

**Hitbox set** (12 bytes): `i32 name (rel)` (`default` on every model but one vehicle with six), `i32 count`, `i32 offset (rel)`. **Hitbox** (32 bytes): `i32 bone`, `i32 group`, `f32 min[3]`, `f32 max[3]` in the bone's space. Groups follow the body: 0 generic, 1 head, 2 chest, 3 stomach, 4/5 left/right arm, 6/7 left/right leg (8 and 9 appear 11 times each). 12,705 hitboxes, all with valid bones and boxes.

**Attachment** (60 bytes): `i32 name (rel)` (`mouth`, `eyes`, `muzzleflash`, `camera_position`, `placementOrigin`, numbers), `i32 flags`, `i32 bone`, `f32 local[3][4]` relative to the bone. 1,137 attachments.

**Flex controller** (20 bytes): `i32 type (rel)` (`eyelid`, `mouth`, `morph`, …), `i32 name (rel)` (`right_lid_raiser`, …), `i32` −1, `f32 min`, `f32 max`. Every key named by the expression sets (see [expressions.md](expressions.md)) is a controller name in some model.

**Flex rule** (12 bytes): `i32 flex`, `i32 op count`, `i32 op offset (rel)`. A rule computes the weight of one flex from the controllers with a small stack program of 8-byte ops, `{i32 op, i32/f32 value}`: 1 pushes a constant, 2 pushes a controller, 3 pushes another flex's weight, 4/5/6 add, subtract and multiply, and 7 is a further two-operand operator (division is assumed, unverified). The value field of operator ops holds leftover bytes. All 9,865 rules leave exactly one value on the stack. Example: `upper_right_raiser = right_lid_raiser × (1 − 0.8 × c4) × (1 − c6)`.

**Mesh flex** (32 bytes): `i32 flex`, `f32 targets[4]`, `i32 vertex count`, `i32 vertex offset (rel)`, `i32 pair`. 15,184 mesh flexes, moving 2,793,996 vertices in all. The targets are a ramp over the flex weight: the morph fades in from `targets[0]` to `targets[1]`, holds until `targets[2]` and fades out by `targets[3]`. Most are `0, 1, 10, 11`. A flex can have several mesh flexes with different ramps. Eyelids, for example, have one mesh flex that closes the lid below the neutral weight and another that raises it above.

**Flex vertex.** The model field at +0xC0 selects one of two record forms:

| +0xC0 | Size | Layout | Used by |
|---|---|---|---|
| 2 | 8 bytes | `u16 index`, `u16 delta direction`, `u16 normal-delta direction`, `u8 delta length`, `u8 normal-delta length` | every flexed model but one |
| 0 | 20 bytes | `i32 index`, `f32 delta[3]`, `u16 normal-delta direction`, `u8 normal-delta length`, `u8` 0 | `mingxiao_transformation.mdl` (deltas up to 121 units, too large for a byte length) |

The index is relative to the mesh. A direction is a code into the 64-step packed normal table used by `Quantized16` vertices (code = 12 × table index; every code in the release is valid). Each length scales its unit direction: the position delta in units of **1/32 inch** (up to about 8 inches), the normal delta in units of **1/128** (up to about 2, the most a unit normal can change). Add the normal delta, then renormalise.

The units were measured, not assumed:

- **Position (1/32):** on 162 heads, scaling the lid-closing delta to make the upper eyelid meet the lower lid gives a median of 1/31.7.
- **Normal (1/128):** keeping morphed normals at unit length, which requires dot(n, d) = −|d|/2, gives 1/119 to 1/134.

`openbl-cli mdl export <model> <out.obj> --flex AU27Z=1` writes a morphed model. Jaw drop, eye closing and smiling all look right on the blood doll.

**Mouth** (20 bytes): `i32 bone`, `f32 forward[3]` (0, −1, 0), `i32 flex`.

**IK chain** (16 bytes): `i32 name (rel)`, `i32 link type` (0), `i32 link count`, `i32 link offset (rel)`. **IK link** (28 bytes): `i32 bone`, two `f32[3]` (zero in every file). The 206 character models each have `rhand`, `lhand`, `rfoot` and `lfoot`, with three links (upper arm, forearm, hand).

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

### Packed normals

Both quantized formats store the normal as an index into a table of unit vectors. The two tables come from one generator with a different number of latitude steps *R*:

- Rings run from −z (index 0) to +z (the last index), at polar angles θⱼ = j·π/R for j = 0…R.
- The poles hold one point. Ring *j* holds n = 4·⌈round(2R·sin θⱼ) / 4⌉ points (round to the nearest integer, then up to a multiple of 4).
- Point *i* of a ring is at azimuth 2π·i/n, starting at +x and running counter-clockwise: (sin θ cos φ, sin θ sin φ, −cos θ).

| Format | Code | *R* | Table size |
|---|---|---|---|
| 1, 16-bit | `u16` = 12 × index | 64 | 5,314 |
| 2, 8-bit | byte 3 = index | 12 | 202 |

**How it was recovered.** For every quantized vertex in the release (592,715 in format 1, 73,729 in format 2), `openbl-cli mdl normal-samples` writes the code and the area-weighted normal of the vertex's LOD 0 triangles. Float-format models show that this geometric normal points exactly opposite to the stored one (mean dot product −0.92), so it is negated. Averaging per code gave a measured table with every entry observed. Reading where the azimuth wraps gave the ring count (*R* = 64); the ring sizes came from a constrained fit, and the rule above reproduces all of them. In 6 rings, 2R·sin θ lies just above a multiple of 4 (60.34, 76.25, 124.16), which is what shows the rounding happens before the round-up.

**Accuracy.** Against each vertex's geometric normal, the formula table's median error is 1.26°, better than the measured averages themselves (1.76°). The 8-bit table fits the data to a median of 1.29°. `mdl_normal_table()` builds both tables, and `MdlVertex::normal` is now filled for every vertex in the release.

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

**Facing and movement conventions.** In animation space a character faces **+x**. The root motion of the 8-way `walk` blend on `move_yaw` shows it: `walk_0` moves along +x, `walk_90` along −y, `walk_270` along +y. So `move_yaw` is the heading of the movement relative to facing, positive clockwise seen from above. The cop posed in `walk_180` faces +x (by the direction of his feet) while moving along −x: a backpedal. The *bind* pose faces −y instead, 90° away; that is the offset noted earlier. Every rendered character is animated, so a renderer only rotates model +x to the entity's yaw.

**Not yet decoded:** the +660 float block and the meaning of the unknown melee fields. No IK data has been found; if the format has any, it is in one of the unknown fields.

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
