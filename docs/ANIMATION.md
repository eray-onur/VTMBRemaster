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

## Not done yet

- **Layers** (`MdlSequence::layers`, e.g. `katana_bobble_layer`): how they combine with the base sequence (added or replacing) is unknown.
- **Root motion** (`MdlAnimation::movements`), which needs entities that move.
- **Confirmation against the original**: fade timing, cycle convention ((N − 1) / fps), and the third fade time.
