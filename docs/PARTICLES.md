# Particle simulation and rendering

How OpenBL runs the effects described in [formats/particles.md](formats/particles.md). Code: `src/particles` (`openbl/particles/particle_system.hpp`), drawn by `render::ParticleRenderer` (`shaders/particle.hlsl`); tests: `tests/test_particle_system.cpp`.

**Everything below is inferred** from the definitions and checked against one capture so far. The original's particle code has not been read.

## Where effects come from

`env_particle` entities (1,121 in the maps) name a definition (`particle_definition`) and start when `active` is 1 and they are not `StartHidden`; the rest are started by scripts (Phase 2; `openbl-viewer --show NAME` starts one by targetname). Not handled yet: `func_particle` volumes (rain), `params_particle` and `params_explosion` (used by scripts), particles attached to props (`prop_haunted`), and the entity's `angles`, `bounds` and `ramp_scale`.

## Simulation

- **Particles and emitters.** The definition is the root particle, placed at the entity. Every particle's `spawn` blocks emit children: `burst` of them when the particle starts, then `rate` per second for as long as it lives.
- **Lifetime.** `frames` at the particle's `fps` (inherited from the parent; default 30), stretched to a random length up to `max_frames` when given. Without `frames` a particle lives for ever (emitters).
- **Placement.** A child starts at its parent's position plus the spawn block's `x`/`y`/`z` and a spherical offset of `radius` at azimuth `theta` and elevation `phi` (degrees).
- **Motion, per second.** `x_speed`, `y_speed`, `z_speed` + `elevation_speed` move it in world axes; `radius_speed` moves it along its spherical direction; `theta_speed`/`phi_speed` turn that direction. `parent_speed` is the share of the parent's later motion it follows (0: stays where emitted).
- **Curves** ([formats/particles.md](formats/particles.md)) are sampled at the particle's age, with one random draw per particle for ranges.

## Drawing

Each particle with a `sprite` (`particles/<sprite>.tga`) is a quad facing the camera, `size` × `width` by `size` × `height` units, rolled by `rotation`; `movealign` stretches it along its motion instead. It is drawn with premultiplied blending, after the translucent world:

- **additive colour** = sprite × (`red`, `green`, `blue`) / 255 × `color` / 255;
- **darkening** = sprite alpha × `mask` / 255 (smoke darkens what is behind it).

Particles are sorted back to front each frame and drawn in batches of up to 1,024 per sprite, with their data in the per-draw shader data.

## Calibration against the original

Capture: the barrel fire among the bums in `sm_hub_1` (`BarrelFireEmitter`, camera 772.13 −2429.87 −53.9, pitch 11.19, yaw 89.05; clip in the git-ignored `docs/gameplay_footages/particle_captures/`). The time-averaged red brightness of a column above the barrel (rows 350 / 370 / 390 / 410 of a 1280 × 720 frame):

| | 350 | 370 | 390 | 410 | Higher up |
|---|---|---|---|---|---|
| Original | 0.09 | 0.50 | 0.75 | 0.12 | background only |
| OpenBL | 0.14 | 0.36 | 0.51 | 0.08 | faint glow |
| Speeds × 0.5, sizes × 2 | 0.09 | 0.35 | 0.62 | 0.20 | less glow |

The flames sit in the right place at roughly the right size, but the original's core is about 1.5 times brighter and its flames do not rise as high. Halving speeds and doubling sizes fits this effect better, but one effect cannot separate the free parameters (speed units, `size` as radius or diameter, colour scale), so OpenBL keeps the plain reading until more effects are captured.

## Not done yet

- Calibration on more effects (steam, smoke, water drops) to settle units and colour scale.
- `collide` blocks (bounce, gravity, friction; decals where particles land), `refract` and `normal` (refracting effects), `flat`, `depth_offset`, `sortfront`, `no_z_test`, `fps` on children, `loop` on spawn blocks.
- The light that fire effects cast (the original lights the bums around the barrel; probably a separate dynamic light).
