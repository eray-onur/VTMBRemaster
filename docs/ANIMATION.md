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

On the GPU, every vertex any flex can move has a slot (a vertex attribute); a character's summed offsets go into its per-draw shader data, one float4 per slot, and the vertex shader adds them to the bind-pose position before skinning. Normal offsets are not applied yet.

Nothing drives the controllers until expressions, dialogue and lip-sync exist (Phase 2). `openbl-viewer --flex NAME=VALUE` sets a controller on every NPC, e.g. `--flex smile=1 --flex jaw_drop=0.6` or `--flex blink=1`.

## Not done yet

- **Layers** (`MdlSequence::layers`, e.g. `katana_bobble_layer`): how they combine with the base sequence (added or replacing) is unknown.
- **Root motion** (`MdlAnimation::movements`), which needs entities that move.
- **Flex drivers**: expressions (`.vfe`, `expressions/*.txt`), lip-sync (`.lip`), choreography (`.vcd`); normal offsets of flexes.
- **Confirmation against the original**: fade timing, cycle convention ((N − 1) / fps), and the third fade time.
