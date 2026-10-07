# Particle effects (`particles/*.txt`)

Bloodlines uses its own particle system, unrelated to Valve's later `.pcf` format. Each of the 1,594 files describes one effect in KeyValues syntax. Parser: `openbl/formats/particle.hpp`; tests: `tests/test_particle.cpp`.

## Files

| Root block | Files | Notes |
|---|---|---|
| `Particle` | 1,583 | effects |
| `` `Particle ``, `aParticle`, `bParticle` | 6 | renamed roots, probably to disable an effect or keep an old version |
| `spawn` | 1 | an emitter with no particle of its own |
| (no name) | 4 | `deform_normal`, `furball_normal`, `furball2_normal`, `suck_normal`: settings for normal-map effects, not particles |

Sprites are images in the same folder: `"sprite" "Dominatedots"` is `particles/dominatedots.tga`. Of 1,033 sprite references, 6 name a missing image. Child emitters name other effect files: 1,371 references, 40 to files that are not shipped. Effects are referenced from entities, disciplines (`D_*`), impacts and other effects.

## Structure

```
Particle {
    "sprite" "dominatedots"  "frames" "30"  "size" "1,20~40"  "color" "0,200(10),0(60)"  ...
    spawn   { "particle" "child_effect"  "rate" "40"  "burst" "1"  "loop" "1"  "theta" "0~360"  "radius" "1" }
    collide { self { "bounce" ".2~.8"  "gravity" "200~300"  "friction" "1~1.5" }
              decal { "particle" "..."  "vdecal_first" "decals/stains/blood1"  "vdecal_last" "..." }
              spawn { ... } }
}
```

Keys are case-insensitive in practice (`Radius_speed` and `radius_speed` both occur), so lookups ignore case.

## Values

Numeric values are **keyframe curves**: comma-separated keys, each written as

- `n`, a number, or `min~max`, a random range;
- optionally followed by `(t)`, the key's time in frames;
- optionally followed by `!`, hold: step to the next key instead of blending.

Examples: `"0,200(10),0(60)"`, `"0~360"`, `"-4,-4,4"`, `"255!,0!,255!,0!"`, `"40,0(15)!"`. One shipped value ends with a stray comma. Two are typos, both reported as diagnostics: `smoke2` `"Rotation" "0,~180~180,360"` and `d_potence_1bp_bodyglow2-normal` `"refract" "0,~1,0"`.

`ParticleCurve::at` evaluates a curve:

- **Key times:** keys without `(t)` are spread evenly over the particle's lifetime.
- **Ranges:** one random draw per particle chooses within every range, so a particle's curve stays coherent.

This reading fits every value in the release, but the timing rules are inferred from the data. They still need confirming against the original (see below).

## Properties

The most common keys, with their probable meaning from names and values. All unconfirmed.

| Key | Uses | Probable meaning |
|---|---|---|
| `frames`, `fps`, `min_frames`, `max_frames` | 1,277, 205, 93, 155 | lifetime in frames, and frame rate |
| `sprite`, `normal` | 1,034, 33 | image; normal map for refracting effects |
| `size`, `width`, `height` | 1,034, 317, 377 | billboard size over life (`width`/`height` override it) |
| `color` / `red`, `green`, `blue`, `mask` | 995 / 877 / 1,008 | tint and opacity over life |
| `rotation` | 799 | roll over life, in degrees |
| `x_speed`, `y_speed`, `z_speed` | ~520 | Cartesian velocity over life |
| `radius_speed`, `theta_speed`, `phi_speed`, `elevation_speed` | ~500 | spherical velocity relative to the emitter |
| `parent_speed` | 662 | how much of the parent's motion is inherited |
| `loop` | 427 | repeat the effect |
| `depth_offset`, `sortfront`, `no_z_test` | 594, 191, 25 | draw order and depth testing |
| `flat`, `movealign` | 108, 368 | orientation: lie flat, or align with motion |
| `refract` | 34 | refraction strength (with `normal`) |
| `spawn`: `particle`, `rate`, `burst`, `loop`, `x`/`y`/`z`, `radius`, `theta`, `phi`, `timescale` | | child emitter: which effect, emission rate and burst, position in Cartesian or spherical coordinates |
| `collide`/`self`: `bounce`, `gravity`, `friction` | 54 | physics on collision |
| `collide`/`decal`: `vdecal_first`, `vdecal_last` | 81 | decals left where particles hit |

## Still open

- **Simulation and rendering** in OpenBL: done, see [PARTICLES.md](../PARTICLES.md).
- **Confirming the semantics** against captures from the original: lifetime and key timing, units of the speeds, the spherical axes, blending (additive or alpha), and sorting.
