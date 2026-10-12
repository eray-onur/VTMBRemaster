# Runtime animation

How OpenBL plays the animations described in [formats/models.md](formats/models.md). Code: `src/anim` (`openbl/anim/animation.hpp`); tests: `tests/test_animation.cpp`.

## Sampling a sequence

A sequence plays one animation, or a **blend grid** of up to 16 × 16 animations laid out along one or two pose parameters (`move_yaw` for running directions, `aim_yaw`/`aim_pitch`, `hit_yaw`).

- **Grid position.** Each axis maps its pose parameter's value from the sequence's blend start to end onto the grid's cells, and the pose blends bilinearly between the (up to) four surrounding animations. Values outside the range are clamped; looping parameters (`wrap` 360, e.g. `move_yaw`) wrap into the range first.
- **Broken cells** (grid entries that reference a missing animation) drop out, and the other weights are renormalised.
- **Cycle.** Cycle 0 to 1 covers frames 0 to N − 1 of each animation, interpolating between neighbouring frames. A sequence lasts (N − 1) / fps of its first animation. Looping sequences wrap; others hold their last frame.
- **Blending poses.** Positions interpolate linearly; rotations use a normalised lerp along the shorter arc.

Checked on Andrei's `run` (9 cells across `move_yaw` −180…180): both ends give the same pose (`run_180`), and values between cells give poses between them.

## Transitions

`AnimationPlayer::play` starts a sequence and fades it in over its `fade_in` time, while the previous sequence keeps playing underneath. Fully covered sequences are dropped. Using the new sequence's fade-in is an assumption; the original could also use the old sequence's fade-out.

Sequences can come from the model's include files (e.g. `models/character/shared/male/*.mdl`); poses are mapped to the model's bones by name.

## In the viewer

NPCs play their idle (the first `ACT_IDLE` sequence), starting at a random cycle. Until AI drives them (Phase 2), `openbl-viewer --npc-sequence NAME` makes every NPC play a sequence by label or activity, and `--pose NAME=VALUE` sets pose parameters, e.g. `--npc-sequence ACT_RUN --pose move_yaw=90` for a sideways run. Root motion is not applied.

## Facial flexes

Code: `openbl/anim/flex.hpp`. A face is driven by **flex controllers** (44 on a typical head: `blink`, `smile`, `jaw_drop`, `right_lid_raiser`, ...), each with a range (0 to 1 on the shipped heads):

1. **Rules** turn controller values into flex weights: one stack program per flex, run in file order so a rule can read flexes computed before it ([formats/models.md](formats/models.md)). Flexes without a rule weigh 0; division by zero gives 0.
2. **Mesh flexes** turn a flex weight into a morph scale with their four-point ramp (fade in, hold, fade out).
3. The **vertex offsets** of every active mesh flex, times its scale, are summed per vertex.

On the GPU, every vertex any flex can move has a slot (a vertex attribute); a character's summed position and normal offsets go into its per-draw shader data, two float4 per slot, and the vertex shader adds them to the bind-pose position and normal before skinning (the lighting renormalises the normal), so creases of a smile shade as they move.

Nothing drives the controllers until expressions, dialogue and lip-sync exist (Phase 2). `openbl-viewer --flex NAME=VALUE` sets a controller on every NPC, e.g. `--flex smile=1 --flex jaw_drop=0.6` or `--flex blink=1`.

## Cloth

Hair, skirts, coats and fur on 53 models are particle cloth ([formats/models.md](formats/models.md)). `anim::ClothSimulation` (`openbl/anim/cloth.hpp`) simulates one piece in model space at fixed 1/60 s steps:

1. **Attached particles** (the first ones of each piece) snap to their skinned positions.
2. **Free particles** move by Verlet integration: their last motion, less 2% damping, plus gravity (386 units/s² down).
3. **Constraints**, four passes per step, use the file's weights. Each pass moves the two particles by the Jakobsen approximation of the length error, d × (rest² / (|d|² + rest²) − ½), times each particle's weight. The weights' magnitudes always sum to 2, and an attached particle weighs 0, so this is exact relaxation. A pendulum built this way settles at its rest length (`tests/test_cloth.cpp`).
   - **First group:** stored rest lengths equal the bind-pose distances, kept both ways.
   - **Second group:** every piece stores a constant fraction s of the squared bind distance (s is 0.001, 0.05, 0.2, 0.4, 0.5, 0.7, 0.8 or 1 per piece; e.g. 0.001 on Jeanette's skirt, 1 on Beckett's coat). OpenBL treats these as minimum distances: the pair may not come closer than √s of its bind distance. Held both ways they would pull every piece with s < 1 together (Jeanette's skirt shrank into a band). As minimums, all 113 pieces in the game stay exactly at rest without gravity (largest drift 0.03 units over 2 s).
4. **Capsules** (bone-relative, moved with their bones) push free particles out to their radius.
5. **Body guard (OpenBL's addition):** the capsules are thin (on Jeanette, radius 4.5 up the spine and 3.5 along each thigh), so a skirt sank into the hips and the skin showed through the back of the waistband. A free particle whose skinned position lies within 8 units of a capsule's surface may not move more than 0.25 units behind that position, measured outward from the capsule's axis. It can still swing out and sideways.
6. **Shape pull (OpenBL's addition):** free particles move 2% of the way back to their skinned position each step, so the authored silhouette survives while the cloth swings. With gravity, every piece settles within 6.3 units of its authored shape.
7. **Reset:** a piece whose particle strays more than 64 units from its skinned position (a teleport) restarts from the skinned shape.

On a character's first frame the cloth simulates 1 s with the first pose, so it starts settled. The GPU draws cloth vertices at their particle's position (the skinning shader reads them from the per-draw data, like flex offsets) with normals from the piece's triangles. The two sides of double-sided cards keep opposite normals. `openbl-viewer --no-cloth` skins cloth like the rest of the model. In a release build, 15 cloth characters (`la_hub_1`) cost about 1 ms per frame.

Unknown and unused: the piece parameter (0.3 to 8; 0.3 on furniture cloth), the per-piece bytes, the vertex data, and the 140-byte settings records. Props with cloth (curtains, lanterns, `temple/panel_cloth`) are still static. Not compared with the running game yet.

## Not done yet

- **Layers** (`MdlSequence::layers`; `openbl-cli mdl sequences` lists them): every layered sequence is a weapon sequence, and every layer a weapon overlay: `*_aim_layer` on the aiming sequences of the firearms, crossbow and throwing stars, `*_idle_layer`, `*_bobble_layer` on weapon runs, `*_relaxed_move_layer` on weapon walks. They play only once characters carry weapons (Phase 2), and how they combine with the base sequence (added or replacing) has to be seen in combat in the original.
- **Root motion** (`MdlAnimation::movements`), which needs entities that move.
- **Flex drivers**: expressions (`.vfe`, `expressions/*.txt`), lip-sync (`.lip`), choreography (`.vcd`).
- **Confirmation against the original**: fade timing, cycle convention ((N − 1) / fps), and the third fade time.
