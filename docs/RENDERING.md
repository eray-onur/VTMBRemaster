# How the original renders

This page describes what the original game's pixel shaders do, in prose. The shaders ship with the retail game as DirectX 8 assembly in `materials/dxshaders/*.psh` (112 files), plus compiled `shaders/*.vcs`. Reading them for this purpose is allowed by [LEGAL.md](LEGAL.md). Their code is never copied into this repository, and OpenBL's shaders are written independently.

The original works in **gamma space**: colours from textures are used as stored, multiplied together, and written to the screen with no linear conversion. OpenBL works in linear space, so each rule below is followed by its linear equivalent.

## World surfaces (`LightmappedGeneric`)

- **Lit colour** = base texture × vertex colour × lightmap × 2 × (overbright ÷ 2). The engine passes the overbright factor as a constant. With the usual overbright of 2, the last factor is 1, so the shader doubles base × lightmap.
- **Lightmap texels.** The lightmaps are stored as linear `ColorRGBExp32` luxels, and the engine converts them to an 8-bit texture before drawing. That conversion is inside the engine binary, so it has not been read, but it has been **measured**: texel = luxel^(1/2.2) ÷ 2.
  - Method: captures of the original compared with OpenBL renders of the same camera ([PARITY.md](PARITY.md)). Per 8×8 block of world surface, the original's gain (capture ÷ base texture) against OpenBL's summed luxels fits gain = 0.97–0.99 × luxel^(0.43–0.45) in every channel, on two captures.
  - The bump-mapped lightmap shader contains a commented-out "quadratic approximation to pow(x, 1/2.2)", which shows that the gamma conversion was moved out of the shader.
  - With the shader's doubling, the output in linear terms is **base × luxel** (scale 1.0). An earlier estimate, texel = (luxel ÷ 2)^(1/2.2), gave a scale of 2.297 and rendered the world 2.3× too bright.
- **Saturation.** An 8-bit texel cannot exceed 1, so the combined lightmap saturates at a linear value of 2^2.2 ≈ 4.59 (inferred from the texel formula, not yet seen in a capture). OpenBL clamps the summed light styles there.
- **Gamma.** The original multiplies in pure gamma 2.2 space; OpenBL shades in linear space with sRGB textures. The two agree except in dark tones, where the sRGB curve's linear toe makes OpenBL up to about 15% darker (seen in the blue channel of the brown `sp_tutorial_1` office).
- **Blending in gamma space.** The original blends translucent surfaces, decals and particles on gamma-encoded values. OpenBL does the same: the window and render targets use plain 8-bit formats, every pixel shader encodes its output to sRGB itself (`shaders/color.hlsli`), and blending works on the encoded values. Opaque output is unchanged (checked pixel for pixel on `sp_tutorial_1`); glass, decals, water and particles change. `DecalModulate` is now exactly the original's 2 × texture × destination on encoded values.
- **Self-illumination**: the result blends from the lit colour to base × self-illumination tint, by the base texture's alpha.
- **Detail textures** (`$detail`): multiplied in at 2× (a mid-grey detail texture leaves the colour unchanged), before the lightmap doubling.
- **Environment maps** (`$envmap`): the envmap × `$envmaptint` is added to the base before the lightmap is applied, so reflections are lit by the lightmap too.
- **Translucent lightmapped surfaces**: base × lightmap × 2, with alpha from the base texture.

## Two-texture blends (`WorldVertexTransition`)

The blend factor is not a vertex attribute. It is stored in the **lightmap texture's alpha**, and the shader uses one minus it: the second base texture shows where that alpha is 0. The result is then lit like other world surfaces. OpenBL keeps the displacement alpha per vertex. That gives the same blend if the engine writes 1 − alpha/255 into the lightmap alpha, which is unverified.

## Bump-mapped lightmaps

Bump-mapped faces have three lightmaps, one per basis direction in tangent space (stored as 4 per style in the BSP; see [formats/maps.md](formats/maps.md)). The shader dots the normal-map normal with each of the three basis axes (saturated at 0) and sums the three lightmaps weighted by those dot products.

## Models (`VertexLitGeneric`)

Lit colour = base texture × colour modulation (`$color`, alpha) × per-vertex lighting × 2 × (overbright ÷ 2). The per-vertex lighting is computed by vertex shaders that are only shipped compiled. How the original computes it has not been read, so OpenBL uses a model fitted to captures, in the same units as the world (lit colour = base × light in linear terms, scale 1.0). One ambient cube per model, at its lighting origin:

- **Direct:** every visible point, spot and emissive-surface world light, with its attenuation and spot cone, spread over the cube's six directions by cosine.
- **Indirect:** 162 rays traced from the point through the BSP (`BspFile::trace_lightmap`) to the first lightmapped surface. Its summed light styles are averaged per cube direction with cosine weights, and weighted by **0.5**.

Against two captures of `sp_tutorial_1`, props in the original are 1.04–1.08 / 0.99–1.06 / 1.08–1.15 as bright as in OpenBL (red / green / blue, geometric mean over props). The props differ from each other by about 18%. Weighting the surfaces by their reflectivity fitted worse. The previous model (direct light plus 30% of the floor lightmap below, at scale 2.297) differed by up to 84% between props and was too blue. Still open: whether the original lights per vertex (its console has `r_staticlighting 1`), sky ambient light for outdoor props, and captures from more rooms to confirm the 0.5. Self-illumination blends to base × tint by base alpha, as for the world. Alpha-tested models discard pixels with a texture-kill instruction driven by a separate coordinate.

## Eyes (`Eyes`)

391 materials: `$basetexture` is the white of the eye, `$iris` an iris disc with an alpha mask. Each eye is its own small mesh (a cap about 0.5 units in radius on human heads; `MdlMesh::material_type` 1, parameter 0 or 1 for the two eyes), and its texture coordinates are an exact front projection of the cap, spanning 0 to 1 across it. OpenBL draws the iris from those coordinates: iris texture at (uv − 0.5) / 0.58 + 0.5, blended by its alpha. The size 0.58 is measured in the original, where an iris spans about 0.58 of the eye opening's width (`sm_hub_1`, the haven bum's dialogue close-up).

Not done: the eyes looking at a target (in dialogue they follow the player), the small specular glint near the pupil, and `$vampire` (10 materials). The model field at +0xd8 holds one record per head with eyelid flex ramps, not eyeball geometry, so the original's projection data is still unknown.

## Unlit surfaces (`UnlitGeneric`)

Base texture × vertex colour, with no doubling.

## Fog and the 3D skybox

**Fog** comes from `worldspawn` (`fogenable`, `fogcolor` 0-255, `fogstart`, `fogend`; 26 maps enable it) and is linear in view depth, blended on encoded colour like Direct3D's fixed-function fog: opaque and alpha-blended surfaces blend towards the fog colour, additive surfaces and particles fade out, `DecalModulate` fades to neutral. Code: `render/fog.hpp`, `shaders/color.hlsli`.

**3D skybox.** 42 maps have a `sky_camera`: a miniature scene (`scale` 16) in a sealed area of the map. In every one the camera's BSP area is an area of its own (`tests/test_bsp.cpp`), so OpenBL draws that area's faces, props and particles as the skybox and leaves them out of the main view. Each frame: the 2D sky, then the skybox seen from the sky camera's origin plus the eye position divided by the scale (same orientation) with the sky camera's own fog, then a depth clear, then the world. The skybox fog's distances are world units, so they are divided by the scale in skybox space: measured on `ch_hub_1` against the original, the distant towers are fully fogged (fraction 1.0) and the street's end gate 0.9, which the scaled distances reproduce and unscaled ones do not (the towers then stay crisp).

**Fog strength (measured).** The original's fog is stronger than a straight line from `fogstart` to `fogend`: it matches 1 − (1 − f)², with f that linear fraction, as if the fog were applied twice. Fog fractions (how far a pixel moves towards the fog colour) measured against the original:

| Place | Original | Linear f | 1 − (1 − f)² (OpenBL) |
|---|---|---|---|
| `ch_hub_1`, far end of the street | 0.30 | 0.15 | 0.28 |
| `ch_hub_1`, far buildings | 0.71 | 0.41 | 0.66 |
| `sm_hub_1`, street lamps half way down the street | 0.37 | 0.18 | 0.32 |
| `sm_hub_1`, far end of the street | 0.61 | 0.36 | 0.59 |

Doubling the distance instead fits worse (it overshoots the far values). Whether fog uses view depth or radial distance is not settled; OpenBL uses view depth.

## Monitors (`func_monitor` + `point_camera`)

A `func_monitor` is a brush entity whose screen faces use a material with `$basetexture engine/camerarendertarget` (in the shipped maps always `dev/dev_tvmonitor1a`: the picture times rolling scanlines, additive). Its `target` names a `point_camera`. Bloodlines cameras aim with a `look_dest` point (their angles are always 0), and set the picture's angle with `FOV`. Some cameras are parented to moving props (`parentname`); OpenBL keeps them at their map position for now.

OpenBL gives each visible monitor its own 512 × 512 render target. Each frame, before the main view, it draws the scene from that monitor's camera into the target, leaving out monitor screens. Checked against the original on two stations ([PROXY_CAPTURE.md](PROXY_CAPTURE.md)): the hospital monitor shows the same room with the same framing and orientation. The haven TV shows the newscaster, logo and scrolling ticker; OpenBL's picture is darker. Not yet confirmed: whether `FOV` is horizontal or vertical (the pictures are square), the original's render-target size, and how bright the picture should be.

**Hidden entities.** Entities with `StartHidden 1` are invisible until a script shows them, so OpenBL does not draw them: brush entities (138 `func_brush`, 164 `func_physbox`, 41 `func_illusionary`, 11 `func_monitor`, ...), `prop_*` entities and NPCs. Examples are the haven TV (off until switched on) and the hospital's `monitor_1_dead`, a static screen shown once its camera is broken; without this the static covered the live picture. Scripts often show such entities as soon as the map loads (the haven bum in `sm_hub_1` starts hidden and greets the player at once), so which ones are visible in play needs the map scripts (Phase 2). `openbl-viewer --show NAME` draws a hidden entity anyway (brush entities, props and NPCs), as `ent_fire NAME ScriptUnhide` does in the original.

## Still to read

Water (`waterreflect.psh`, `waterrefract.psh`), eyes, the envmap and mask variants, and the compiled vertex shaders, which hold the vertex lighting and fog.
