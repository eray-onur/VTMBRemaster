# How the original renders

This page describes what the original game's pixel shaders do, in prose. The shaders ship with the retail game as DirectX 8 assembly in `materials/dxshaders/*.psh` (112 files), plus compiled `shaders/*.vcs` (plain shader bytecode with constant tables, one shader per combination). Reading them for this purpose is allowed by [LEGAL.md](LEGAL.md). Their code is never copied into this repository, and OpenBL's shaders are written independently.

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
- **Environment maps** (`$envmap`). The DX8 `.psh` files have two orders: the older shaders add the envmap to the base before lighting, the `v2` ones after. At DX9 the game uses `lightmappedgeneric_ps20` (compiled), which adds it after lighting:
  - lit colour + envmap(R) × `$envmaptint`, with R the eye vector reflected about the vertex normal;
  - then `$envmapcontrast` lerps towards the colour squared, and `$envmapsaturation` lerps from its grey (mean of the channels);
  - an optional Fresnel term (`$fresnelreflection`) scales it;
  - everything on encoded values.

  The mask is `$envmapmask` (colour), 1 − base alpha with `$basealphaenvmapmask`, or the bump map's alpha with `$normalmapalphaenvmapmask`.

  Other parameters:
  - **`$fresnelreflection`** (help text in the shader DLL: "0.0 == no fresnel, 1.0 == full fresnel") is set by no shipped material. The default is no Fresnel, and the road brightness above confirms it: full Fresnel would remove almost all of the near road's reflection.
  - **`$envmapmode` and `$envmapcameraspace`** are registered only by `MaterialSystem.dll`, as generic material flags. No shader reads them, so OpenBL ignores them too.
  - **`$envmapsphere`** marks a 2D sphere map (a mirrored ball seen from the camera) instead of a cube. Only three real materials use it: `dev/dev_envmap` and the two breakable-glass shard materials. OpenBL resamples sphere maps into 32 × 32 cube maps at load time, as if seen looking along +x, so the shaders keep one lookup.

  **Cubemaps** are in each map's pakfile as `materials/maps/<map>/c<x>_<y>_<z>` (32 × 32, 7 faces, the 7th a sphere map) plus `cubemapdefault`. The map compiler writes per-cubemap `patch` copies of the materials that use one (`<material>_<x>_<y>_<z>.vmt`, inserting `$envmap`). Materials it left with `env_cubemap` get the default cubemap. Faces are in Source's world axis order, so a world-space reflection vector samples them directly, as a cube map.

  **Wet streets:** street materials drive `$envmaptint` with `GlobalWetness`, so they only shine in rain. The road outside the pawn shop on `sm_hub_1` is not one of them. Measured against the original (camera −1839.87 −2179.87 −40, pitch 9.22, yaw 325.05), its mean colour is 74 65 45 near and 94 82 57 further away. OpenBL gives 69 60 37 and 93 81 55 with environment maps, and 49 38 17 and 80 67 39 without.
- **Translucent lightmapped surfaces**: base × lightmap × 2, with alpha from the base texture.

## Two-texture blends (`WorldVertexTransition`)

The blend factor is not a vertex attribute. It is stored in the **lightmap texture's alpha**, and the shader uses one minus it: the second base texture shows where that alpha is 0. The result is then lit like other world surfaces. OpenBL keeps the displacement alpha per vertex. That gives the same blend if the engine writes 1 − alpha/255 into the lightmap alpha, which is unverified.

## Bump-mapped lightmaps

Faces whose texinfo has `SURF_BUMPLIGHT` (0x800) store four lightmaps per style: the flat one, then one per basis direction in tangent space ([formats/maps.md](formats/maps.md)). 23,279 faces in 59 of the 101 maps have it, most in the Warrens, the Chinatown temple, the Santa Monica and Ash sewers. 19,560 faces over all maps have both the flag and a `LightmappedGeneric` material with a `$bumpmap`, mostly sewer, Warrens and temple walls.

The original's `lightmappedgeneric_ps20` (read from the shipped `.vcs`) lights those faces from the three directional lightmaps only:

- the normal is the bump map's texel × 2 − 1, not renormalised;
- each weight is the normal dotted with one basis direction, clamped at 0: (0.816497, 0, 0.57735), (−0.408248, 0.707107, 0.57735) and (−0.408248, −0.707107, 0.57735);
- light = (w₀ L₁ + w₁ L₂ + w₂ L₃) / (w₀ + w₁ + w₂), a weighted average;
- the vertex shader places the three lightmaps 1, 2 and 3 lightmap widths to the right of the flat one, and samples the bump map at the untransformed base coordinates (`$bumptransform` only appears on water-like materials).

The average is taken on the lightmap texture's encoded values (luxel^(1/2.2) / 2, as for flat lightmaps), so OpenBL sums each direction over the styles in linear terms, then averages them as L^(1/2.2) and converts back. Over ch_temple_3's 2,402 bump-lit faces the three directions average 0.97 of the flat lightmap, but the encoded-space average is lower where they differ: the temple start view comes out 17% darker than with flat lightmaps, a sewer wall 9%. Not yet compared with the original.

Not done: the bumped normal in environment-map reflections (23 of the 61 bump materials have an `$envmap`; OpenBL reflects about the face normal), `WorldVertexTransition` blends of two normal maps (they keep the flat lightmap), and normal maps on models. Bump-mapped materials on faces without the flag use the flat lightmap, as there is nothing else. `openbl-viewer --no-bump` uses the flat lightmap everywhere.

## Models (`VertexLitGeneric`)

Lit colour = base texture × colour modulation (`$color`, alpha) × per-vertex lighting × 2 × (overbright ÷ 2). The per-vertex lighting is computed by vertex shaders that are only shipped compiled. How the original computes it has not been read, so OpenBL uses a model fitted to captures, in the same units as the world (lit colour = base × light in linear terms, scale 1.0). One ambient cube per model, at its lighting origin:

- **Direct:** every visible point, spot and emissive-surface world light, with its attenuation and spot cone, spread over the cube's six directions by cosine.
- **Indirect:** 162 rays traced from the point through the BSP (`BspFile::trace_lightmap`) to the first lightmapped surface. Its summed light styles are averaged per cube direction with cosine weights, and weighted by **0.5**.

**Environment maps on models** (1,071 `VertexLitGeneric` materials) are added the same way as on the world (the `v2` model shaders add envmap × mask × tint after lighting). A model with `$envmap env_cubemap` uses the map cubemap nearest to it: its lighting origin for props, its chest height for NPCs. Checked on `sm_hub_1`'s alley manhole (a prop, station 4 in [PROXY_CAPTURE.md](PROXY_CAPTURE.md)): mean colour 14.8 13.5 9.6 in the original, 13.9 12.4 9.6 in OpenBL, and 4.3 1.8 0.2 without the envmap. On characters it is mostly small parts, such as the police officers' sunglasses.

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

## Sprites (`env_sprite`)

`sm_hub_1` has 309 sprites: street-lamp glows, signal lights, police lights and a row of light cones in the 3D skybox. Of these, 265 have the "start on" flag (spawnflags 1); the rest wait for a script (traffic-light colours, for example). Each is one quad:

- **Material:** the `Sprite` shader (61 materials). `$spriteorientation` says how the quad faces the camera:
  - `vp_parallel` (the default): in the view plane;
  - `parallel_upright`: upright, turned about z;
  - `oriented`: fixed by the entity's angles;
  - `vp_parallel_oriented`: in the view plane, rolled by the entity's roll.

  `$spriteorigin` is the point of the picture placed at the entity's origin.
- **Size:** texture width and height × `scale`, in world units. The skybox's light cones (`volumelighta`, 256 pixels × 0.06 ≈ 15 skybox units, so 240 world units) fit this.
- **Colour:** the texture × `rendercolor` × `renderamt` / 255, on encoded values.
- **Render modes:** additive for `rendermode` 5 (`kRenderTransAdd`), 3 (`kRenderGlow`) and 9 (`kRenderWorldGlow`, 66 sprites over all maps), alpha-blended otherwise. Animated sprites step at the entity's `framerate`. Additive sprites fade out in fog.
- **Effects:** only `renderfx` 0, 2 and 14 occur. 2 (`kRenderFxPulseFast`, 12 sprites) pulses the amount by 16 / 255 × sin(8 t + entity index × 363), as in Source. 14 ("no dissipation") changes nothing in Bloodlines, because its client code has no distance fade for glows. No sprite sets `GlowProxySize`.
- **Glows** (`rendermode` 3 and 9) are depth-tested like other sprites. In the client DLL (`client.dll`, read from its code), the glow brightness is multiplied by the visible fraction of an octagonal occlusion proxy drawn at the glow's centre: 3 units in radius, or distance × 3/128 for mode 3, so that proxy keeps one size on screen. The sprite itself always has its world size: the scale function only animates the scale over time.
  - Only the world hides a glow's centre. 54 `sm_hub_1` glows sit inside the opaque globes of the curved street lamps (`pierlightb.mdl`). The original still draws them at full strength: the globe covers the middle of the glow, while the pole, palms, columns and NPCs behind the glow's quad are brightened. Measured with `r_drawsprites 0` against 1 from below and from above a globe (stations `lamp` and `c1` below); the radial profiles match OpenBL's within 2 to 3 levels out to 300 pixels.
  - OpenBL traces a line through the world each frame instead of the occlusion query.
  - An earlier OpenBL version hid glows enclosed by a prop, from a comparison with no sprites-off baseline from the original. The on/off captures showed that was wrong.

**Fades from material proxies**, evaluated per sprite with its distance and direction from the camera:

- **`PlayerProximity`** (the light cones, `volumelight*_proxyfade`): `$alpha` = distance × 0.001 − 0.2. A cone is invisible within 200 units and full at 1,200, so the flat card disappears as you walk under it. In the 3D skybox the distance is multiplied by the skybox scale.
- **`LampBeam`** (the street-light beams, `streetlight*_proxyfade`; read from `client.dll`): `$alpha` = 1 − |z|, with z the vertical part of the normalized camera-to-sprite direction. A beam fades as it is seen from below.
- **`LampHalo`**: `$alpha` = clamp((|z| − 0.25) × 1.35, 0, 1). The halo appears as you look up or down at the lamp.

**Checked against the original** on `sm_hub_1`. Each station was captured with sprites on, off (`r_drawsprites 0`, which also hides rain and the cables) and on again, so the sprites' share is measured directly. OpenBL's own share comes from a render against one with `--no-sprites`. The table gives OpenBL's summed sprite light over the original's, per channel; the original's sums also contain rain and cables.

| Station | Camera (x y z, pitch / yaw in OpenBL's convention) | Sprites | OpenBL / original (R G B) |
|---|---|---|---|
| `j1` | −1179.87 350.13 −39.94, 10.49 / 266.08 | traffic lights, `rendermode` 1 (green and yellow on, red off; the original's `streetlight_timer` disabled and the sprites set with `ent_fire` to match) | 0.93 0.97 0.99 |
| `w3` | −2019.87 −2049.87 40, −2.68 / 83.67 | the tattoo shop's wall lamps, `rendermode` 5 | 1.00 1.00 1.01 |
| `c1` | −1999.87 −2099.87 40, 14.04 / 182.88 | skyline light cones (3D skybox) and a lamp-globe glow | 1.03 1.04 1.06 (cones alone 0.87) |
| `lamp` | −1839.87 −2179.87 −39.94, 13.35 / 58.92 | a lamp-globe glow from below | 1.39 1.43 1.91 (about 2 levels brighter across a wide, faint ring) |

Not done: scripts switching sprites on and off (the traffic lights cycle every 40 s through `logic_relay` skins and sprites), and scale animations. `openbl-viewer --no-sprites` hides all sprites.

## Translucent order

The original draws translucent things in BSP order, back to front from the eye, as Source does:

- **World surfaces.** A face lying on a BSP node is drawn between the node's far and near sides. Detail faces lie on no node (7,380 of sm_hub_1's 11,906 world faces); each is drawn in the nearest leaf whose face list holds it.
- **Entities** (props, characters, sprites, brush entities) are registered in every leaf their bounds touch and drawn in the nearest one. In a leaf, the world surfaces come first, then the entities, farthest first.

OpenBL follows this with one queue per view (`render/translucent.hpp`, `formats/draw_order.hpp`): each renderer lists its translucent items with their BSP slot and distance, and the queue draws them in order. Water comes first. A particle effect is one item, placed by the bounds of its current particles. Sprite bounds are half the picture's larger side around the origin; props use their model bounds, characters a 32 × 32 × 72 box. With the skyline light cones of `sm_hub_1`, placing sprites by their origin alone put the skybox's translucent building facades over them (0.60 of the original's cone light); with their bounds the cones measure 0.87 as before.

## Static prop fades

7,920 of the 24,409 static props fade out with distance: those with flag bit 0, which every prop with a fade distance has. Read from the original's `engine.dll`: when the map loads, it keeps min², max² and 255 / (max² − min²) per fading prop. Each frame, from the main camera's position (client.dll passes it once per frame, with a factor of 1 unless the player zooms), it takes the squared distance d² to the prop's origin:

- d² ≥ max²: not drawn;
- d² ≤ min²: drawn normally;
- otherwise drawn translucent with opacity (max² − d²) / (max² − min²), linear in the *squared* distance.

The opacity is kept on the prop and used in every view, so props in the 3D skybox or seen in a monitor fade by their distance from the player, not from that view's camera. Most props fade between 2,250 and 2,500 units, or 1,500 and 2,000; some `sm_hub_1` building pieces already from 760 to 1,280. No prop entity uses `fademindist`/`fademaxdist` (the six that set them turn the fade off).

OpenBL draws a fading prop whole in the translucent queue: opaque meshes alpha-blended by the opacity without writing depth, translucent ones with their alpha times it. `openbl-viewer --no-prop-fade` keeps every prop solid. Not yet compared with the running game.

## Monitors (`func_monitor` + `point_camera`)

A `func_monitor` is a brush entity whose screen faces use a material with `$basetexture engine/camerarendertarget` (in the shipped maps always `dev/dev_tvmonitor1a`: the picture times rolling scanlines, additive). Its `target` names a `point_camera`. Bloodlines cameras aim with a `look_dest` point (their angles are always 0), and set the picture's angle with `FOV`. Some cameras are parented to moving props (`parentname`); OpenBL keeps them at their map position for now.

OpenBL gives each visible monitor its own 512 × 512 render target. Each frame, before the main view, it draws the scene from that monitor's camera into the target, leaving out monitor screens. Checked against the original on two stations ([PROXY_CAPTURE.md](PROXY_CAPTURE.md)): the hospital monitor shows the same room with the same framing and orientation. The haven TV shows the newscaster, logo and scrolling ticker; OpenBL's picture is darker. Not yet confirmed: whether `FOV` is horizontal or vertical (the pictures are square), the original's render-target size, and how bright the picture should be.

**Hidden entities.** Entities with `StartHidden 1` are invisible until a script shows them, so OpenBL does not draw them: brush entities (138 `func_brush`, 164 `func_physbox`, 41 `func_illusionary`, 11 `func_monitor`, ...), `prop_*` entities and NPCs. Examples are the haven TV (off until switched on) and the hospital's `monitor_1_dead`, a static screen shown once its camera is broken; without this the static covered the live picture. Scripts often show such entities as soon as the map loads (the haven bum in `sm_hub_1` starts hidden and greets the player at once), so which ones are visible in play needs the map scripts (Phase 2). `openbl-viewer --show NAME` draws a hidden entity anyway (brush entities, props and NPCs), as `ent_fire NAME ScriptUnhide` does in the original.

## Water (`Water`)

90 materials. At DirectX 9 the original draws them with the shader it registers as `Water_Old_dx80_dx81_dx90`: the vertex shader `water_vs20_old` and the pixel shaders `waterrefract_ps20_old` and `waterreflect_ps20_old` (in `shaders/fxc/`; the newer single-pass `water_ps20` ships too but nothing registers it). Read from the compiled code:

- **Two passes.** The refraction pass draws the refraction target (the scene below the surface) times `$refracttint`. The reflection pass draws the reflection target times a Fresnel weight, (1 − max(cos, 0))⁴, where cos is between the view direction and the normal-map normal in the surface's tangent frame. No `$reflecttint` appears in the DX9 shaders. OpenBL adds the two (the blend states are set by the engine binary and are not read).
- **Distortion.** Both targets are sampled at the pixel's screen position plus an offset from the dudv map (`$bumpmap`, signed bytes; [formats/textures.md](formats/textures.md)). The offset is a displacement along the surface: amount (`$refractamount` or `$reflectamount`) × (du × tangent + dv × binormal), in world units, projected by the view-projection matrix and divided by the pixel's depth. The result is used directly as a texture offset (twice the size of the true projected shift). The dudv and normal maps share a transform (`$bumptransform` or `$bumpoffset`, scrolled by `TextureScroll`) and step through their 29 frames together.
- **Water fog.** While drawing what lies below a water surface, the world's vertex shaders fog by the length of the view ray under the water: view depth × max(water z − z, 0) ÷ (eye z − z), with the material's `$fogcolor`, `$fogstart` and `$fogend`.

Every water material names the same two targets, `_rt_WaterReflection` and `_rt_WaterRefraction`, so the original has one reflection and one refraction per frame, for one surface height. OpenBL does the same. OpenBL groups the upward-facing water faces by height (within 2 units) into water planes (21 of the 101 maps have water, with 1 to 12 planes; the Warrens have the most), picks the nearest plane in view, and draws the two targets at the window's size before the main view. Water at other heights samples the same targets, as in the original:

- **Reflection:** the camera mirrored about the plane, with the projection's near plane moved onto the water (oblique clipping), so only what is above the surface is drawn. The picture is also flipped left to right, which keeps the triangle winding.
- **Refraction:** the normal camera, clipped to what is below the surface (a unit of overlap keeps the shore closed), in the water fog.

The colours combine on encoded values, as in the original. Materials without `$refracttexture` show their fog colour instead, as does all water with `openbl-viewer --no-water`. Water and monitors are left out of both targets. Faces using a water material that do not face up (the undersides, which use `$bottommaterial`, and the sides) are only seen from below the water and are skipped for now. Across the `ch_lotus_1` pool at 1600 × 900, the two passes take a release build from about 6 to about 8 ms per frame (a debug build from 19 to about 29 ms).

**Checked** on `ch_lotus_1` (station 10 in [PROXY_CAPTURE.md](PROXY_CAPTURE.md), looking down into the pool): the floor shows through and warps as much as in the original. The warp amplitude, estimated as temporal change ÷ spatial gradient over 24–60 frames, is 3.7 px against the original's 3.8 px on lightly blurred frames, and 5.6 against 6.8 px on more blurred ones. The original's warp breaks into horizontal streaks a few pixels tall, which OpenBL does not reproduce; they look like how the hardware samples the signed dudv texture. Colours over the pool are within 10% except blue (18 against 22 of 255), but the stone rim beside the pool is about 30% darker in OpenBL as well, so that difference is the room's lighting, not the water.

**Reflections differ, deliberately.** From a level view across the same pool (camera 760.13 0.13 −194.94), the original's far half of the pool is almost black: the refraction fogs out along the long underwater ray, and no reflection of the red beams or lanterns shows. OpenBL reflects them clearly at that angle. Whether the original drops the reflection there (blend state, cheap water, or the shader's constants) is not settled. OpenBL keeps its reflections as an improvement rather than matching the black water.

**Cheap water.** The original's `watercheap_ps20_old` draws `$fogcolor` plus the water's `$envmap`, sampled with the eye vector reflected about the normal-mapped normal and weighted by (1 − cos)² (not the expensive shader's fourth power), with no refraction or tint. OpenBL blends from expensive to cheap water between `$cheapwaterstartdistance` and `$cheapwaterenddistance` from the eye, per pixel (the help text in the shader DLL calls this a transition). `$forcecheap` water is cheap everywhere. In the 3D skybox, the distances are divided by the skybox scale, as for its fog, and cheap water needs no render targets. So the open sea around `la_dane_1` (skybox water, `dev_waterbeneath2`, cheap from 500 to 1000 units) now shows the cubemap's waves instead of flat fog colour.

**Not done:** the view from below the surface (`$bottommaterial`, and fog around an underwater camera), expensive water in the 3D skybox (no targets there; it shows its fog colour until cheap water takes over), and `WaterWarp`.

## Still to read

Eyes, the envmap and mask variants, and the compiled vertex shaders, which hold the vertex lighting.
