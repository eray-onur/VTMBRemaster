# Particle simulation and rendering

How OpenBL runs the effects described in [formats/particles.md](formats/particles.md). Code: `src/particles` (`openbl/particles/particle_system.hpp`), drawn by `render::ParticleRenderer` (`shaders/particle.hlsl`); tests: `tests/test_particle_system.cpp`.

**Sources.** The game ships a commented template, `particles/000_template.txt`, written by the developers; it documents most keys, and the rules below follow it where it speaks (summarised here in our own words). The rest is inferred from the definitions and checked against a capture. The original's particle code has not been read.

## Where effects come from

`env_particle` entities (1,121 in the maps) name a definition (`particle_definition`) and start when `active` is 1 and they are not `StartHidden`; the rest are started by scripts (Phase 2; `openbl-viewer --show NAME` starts one by targetname). Not handled yet: `func_particle` volumes (rain), `params_particle` and `params_explosion` (used by scripts), particles attached to props (`prop_haunted`), and the entity's `bounds` and `ramp_scale`.

## Simulation

- **Particles and emitters.** The definition is the root particle, placed at the entity. Every particle's `spawn` blocks emit children: `burst` of them when the particle starts (the template says bursts can fire on chosen frames; only the first is handled), then `rate` per second while it lives (per inch of movement with `distance 1`, not handled yet).
- **Frames and lifetime.** `frames` at the particle's `fps` (inherited from the parent; default 30) is the span its keyframes are spread over. The lifetime is `frames`, or a random length between `min_frames` (or `frames`) and `max_frames`. With `loop 1` a particle never dies and its keyframes repeat every `frames`. Without `frames` it lives for ever (emitters).
- **System axes.** `x`/`y`/`z` offsets and `x_speed`/`y_speed`/`z_speed` run along the system's axes, which turn with the entity's `angles`; `elevation` and `elevation_speed` are always world up.
- **Placement.** A child starts at its parent's position plus those offsets and a spherical offset of `radius` at azimuth `theta` and elevation `phi` (degrees).
- **Motion, per second.** `radius_speed` moves it out from its spawn point; `theta_speed`/`phi_speed` turn it horizontally/vertically around it. `parent_speed` is the share of the parent's motion it follows (0: stays where emitted, 1: moves with the parent).
- **Spawn modulation.** A spawn block's `red`/`green`/`blue`/`color`/`mask` (0 to 255) and `size`/`width`/`height` scale its children, and its `rotation` adds to theirs, using the block's values at the parent's current age. `timescale` stretches the children's lifetimes (2: twice as long), e.g. 8 on the moths' wing-beat frames.
- **Curves** ([formats/particles.md](formats/particles.md)) are sampled at the particle's age, with one random draw per particle for ranges.

## Drawing

Each particle with a `sprite` (`particles/<sprite>.tga`) is a quad facing the camera, `size` × `width` by `size` × `height` units, rolled by `rotation`; `movealign` stretches it along its motion instead. It is drawn with premultiplied blending. Each effect is one item in the translucent queue ([RENDERING.md](RENDERING.md#translucent-order)), placed by the leaves its particles reach, with its particles back to front:

- **added light** = the sprite's colour × (`red`, `green`, `blue`) / 255 × `color` / 255, on gamma-encoded values (the sprites are already dark where transparent, so their alpha is not applied again);
- **darkening** = sprite alpha × `mask` / 255: the background is scaled by one minus that (smoke darkens what is behind it).

Blending happens in gamma space, as in the original (see [RENDERING.md](RENDERING.md)).

Particles are sorted back to front each frame and drawn in batches of up to 1,024 per sprite, with their data in the per-draw shader data.

## Calibration against the original

Capture: the barrel fire among the bums in `sm_hub_1` (`BarrelFireEmitter`, camera 772.13 −2429.87 −53.9, pitch 11.19, yaw 89.05; clip in the git-ignored `docs/gameplay_footages/particle_captures/`). The time-averaged red brightness of a column above the barrel (rows 350 / 370 / 390 / 410 of a 1280 × 720 frame):

| | 350 | 370 | 390 | 410 | Higher up |
|---|---|---|---|---|---|
| Original | 0.09 | 0.50 | 0.75 | 0.12 | background only |
| OpenBL | 0.14 | 0.36 | 0.51 | 0.08 | faint glow |
| Speeds × 0.5, sizes × 2 | 0.09 | 0.35 | 0.62 | 0.20 | less glow |
| Template semantics | 0.10 | 0.29 | 0.48 | 0.08 | faint glow |
| ... with gamma-space blending, alpha only masking | 0.10 | 0.43 | 0.63 | 0.07 | glow about 0.1 (original about 0.04) |

With the template's semantics and gamma-space blending the flames sit in the right place at the right size, the base of the column matches and the core reaches about 0.85 of the original's brightness (from 0.6 with linear blending). The haze above the fire is now too bright (about 0.1 against 0.04), probably the large smoke sprites' added light; more captured effects should show whether smoke is drawn differently.

## Not done yet

- Calibration on more effects (steam, smoke, water drops): the smoke above the barrel fire is too bright.
- Bursts on later frames, `distance` emission, `depth_offset`, `no_z_test`, `flat`, `sortfront`, `lighting` (scaled by scene lighting), `precipitation`.
- `collide` blocks (bounce, gravity, friction; decals where particles land), `refract` and `normal` (refracting effects).
- The light that fire effects cast (the original lights the bums around the barrel; probably a separate dynamic light).
