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
- **Gamma 2.2.** The original's colour is pure gamma 2.2, and OpenBL uses the same curve, not sRGB. Textures, the window and render targets are plain 8-bit formats holding encoded values, so texture filtering also happens on encoded values, as on DX9 without sRGB reads. Shaders decode a texture's colour with x^2.2 where light multiplies it and encode the result with x^(1/2.2) (`to_linear`, `to_gamma` in `shaders/color.hlsli`). Sprites, particles, water's render targets and environment maps use the encoded values directly. With sRGB, the curve's linear toe made dark tones too dark: on `sp_tutorial_1` the view below the ceiling (mostly world) measured 0.89 / 0.84 / 0.71 of the original's encoded brightness, and 1.02 / 1.01 / 1.01 with gamma 2.2. In the `sm_hub_1` alley (station 4 in [PROXY_CAPTURE.md](PROXY_CAPTURE.md)), the road's blue went from 3.8 to 9.8 (the original: 8.0) and a wall's from 2.9 to 8.4 (17.6).
- **Blending in gamma space.** The original blends translucent surfaces, decals and particles on gamma-encoded values. OpenBL does the same: the window and render targets use plain 8-bit formats, every pixel shader encodes its output itself, and blending works on the encoded values. `DecalModulate` is exactly the original's 2 × texture × destination on encoded values.
- **Which faces are drawn.** A face is hidden by its texinfo flags (no-draw, sky, trigger, skip, hint), which vbsp sets from the material's `%compile*` keys, or by its brush entity's class: triggers, `func_dustmotes`, `func_particle` and `inspection_brush` never draw their brushes (760 faces of `trigger_hurt` have a plain texture). Other materials in `tools/` are ordinary surfaces: `tools/black` is the ceiling-fan rod in `sp_tutorial_1` and blacks out openings in 31 maps; `toolsblack`, `fade1` (a translucent fade, under the `sm_pier_1` pier) and `grid` (hidden behind bar shelving) also appear on world faces.
- **Self-illumination**: the result blends from the lit colour to base × self-illumination tint, by the base texture's alpha.
- **Detail textures** (`$detail`): multiplied in at 2× (a mid-grey detail texture leaves the colour unchanged), before the lightmap doubling. The DX8 shaders do the same on models (after lighting) and unlit surfaces, all on gamma-space values. In the shipped data only the `shadertest/` materials and the two cracked breakable-glass materials (`glass/breaksurf/break_glass_*_cracked`, `ShatteredGlass`) name a `$detail`; no map pakfile does. So nothing in the shipped maps shows one yet; it matters for broken glass (Phase 2) and for mods.

  OpenBL multiplies the encoded colour by 2 × the raw detail texel, after lighting and self-illumination and before the environment map, on the world, props and characters (`apply_detail` in `shaders/color.hlsli`). The detail coordinates are the untransformed base coordinates × `$detailscale`. Its default of 4 is from the Valve Developer Community's `$detail` page; the shader DLLs register the parameter, but its default is set in code, so it is not confirmed for this engine. Checked by mounting a scratch mod that gives 120 of `sp_tutorial_1`'s world materials `shadertest/detail` (mean texel 127.5): the walls gain its pattern and keep their brightness (ratio 1.02), and renders without a `$detail` are pixel-identical to before.
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

**Environment maps.** With a `$bumpmap`, the reflection is about the bumped normal, in world space:

- the vertex shader passes a tangent frame (tangent, binormal, normal), and the pixel shader takes n.x × tangent + n.y × binormal + n.z × normal;
- the tangent and binormal come from the engine. The map data shows they are the texinfo's u and v texture axes, made perpendicular to the face (+u, +v). With that frame, the three directional lightmaps agree with where the strongest light is for 76–80% of the bump faces on ch_temple_3, hw_warrens_1, hw_warrens_3 and hw_ash_sewer_1; the other sign choices score 21–65%;
- the mask is the normal map's alpha, in every combination: none samples an `$envmapmask`, so materials that name both (`metal/danewll*`, `stone/libcolumn*`) use the normal map's alpha, and so do the three without `$normalmapalphaenvmapmask` (`concrete/sewflra`, `concrete/zhaflra`, `objects/manholecovera`);
- the mask multiplies the cube map's colour before `$envmaptint` and `$envmapcontrast`, not after as for flat surfaces;
- some combinations add a Fresnel factor, (1 − n̂·ê)^5 scaled and offset by a constant the engine sets. No material sets `$fresnelreflection`, so OpenBL leaves it out.

This applies to the faces without directional lightmaps too: they are lit from the flat lightmap, but still reflect about the bumped normal.

**Two-texture blends.** `worldvertextransition_ps20` has no bump-mapped combinations, but `lightmappedgeneric_ps20` has combinations with a second base texture and a normal map. Only `WorldVertexTransition` materials have a second base texture, so its bump-mapped blends use these. The normal map then serves both layers, and the envmap mask is its alpha too. Combinations with a `$bumpmap2` blend the two normal maps by the same factor as the two base textures, but no shipped material has one. Five blends have a `$bumpmap` (`blends/warrtunnela`, `warrtrash`, `seablend`, `searock_tunnel`, `veslavarock`, on 8 maps). Their `$bumpbasetexture2withbumpmap` is 0 on two of them, yet no ps20 combination leaves the second layer unbumped, so OpenBL bumps both. On `sp_soc_3`'s tunnel walls, the result keeps more of the warm lantern light and less of the blue fill than the flat lightmap, as the three directions separate them.

Bump-mapped materials on faces without the flag use the flat lightmap, as there is nothing else. `openbl-viewer --no-bump` uses the flat lightmap and unbumped reflections (with the material's own envmap mask) everywhere, and vertex lighting on models.

## Models (`VertexLitGeneric`)

At DX9 the original draws models with `vertexlit_and_unlit_generic_vs20` and `_ps20` (read from the shipped `.vcs`: 1,408 vertex shader combinations, 8 static × 176 dynamic). The pixel shader multiplies the base texture by the interpolated lighting and `$color`, with no doubling and no clamp. The vertex shader lights each vertex with the sum of:

- **an ambient cube**: six colours, weighted by the squared components of the world-space normal, the sign of each component picking the + or − face;
- **up to two lights**, each directional, point or spot (the dynamic combinations cover none, one or two of any kind). Each gives colour × max(n·l, 0). Point and spot lights are also multiplied by 1 / (constant + linear × d + quadratic × d²) and by saturate(1 − d² × w), with w a per-light constant. Spot lights add saturate((cos − outer cosine) × scale)^exponent;
- **a baked per-vertex colour** (a second colour stream), in some combinations: multiplied by the overbright factor, then raised to the power 2.2. This is the per-vertex lighting the engine bakes for static props (its console has `r_staticlighting 1`). Combinations exist with it alone and with it plus the cube and lights.

**Half-Lambert.** A static combination replaces max(n·l, 0) with (n·l × 0.5 + 0.5)². The `$halflambert` material flag selects it, and no shipped material sets that flag (0 of 11,049), so the original lights models with plain Lambert. The bump-mapped shader has the same switch.

The engine computes what it passes in. How it builds a model's lighting, and the baked colours, was read from `engine.dll` and `StudioRender.dll` (below); w, the range term of the shader path, is still unknown.

**Units.** The vertex shader treats light as linear (it raises the baked colours to 2.2), and the pixel shader multiplies it straight onto gamma-space texels. Yet in two captures of `sp_tutorial_1`, each prop's brightness in the original (divided by its base texture) follows OpenBL's light level with a slope well below 2.2 (0.88–1.06 per channel measured through the sRGB curve with the earlier indirect weight of 0.5; 1.2–1.4 in gamma-2.2 terms, which the lower weight below accounts for). So the screen colour is base × light in OpenBL's linear terms, as for the world (scale 1.0), and the engine must pass gamma-encoded colours. OpenBL evaluates the shader's formula in linear terms.

**The original's lighting state.** Read from `engine.dll` (black-box; what it does, not how it is written). For each model the engine keeps a lighting state sampled at one point: for static props the lighting origin from the map's static-prop lump when the prop sets one (flag bit 2), otherwise the model header's illumination point (the vector at 0xa8, which in the shipped models is the centre of their bounds) moved into the world by the prop's origin and angles. The state has an ambient cube and up to `r_worldlights` local lights (default 2; the defaults of `r_radiosity` 2 and `r_worldlightmin` 1/128 below are also from the binary, and no shipped config changes them):

- **Indirect light** (`r_radiosity` 2): 162 rays from the point, 57,016 units long, each to the first surface it meets. A ray that meets a sky face takes the intensity of the map's `light_environment` ambient light; any other surface gives the light in its lightmap at the hit (the luxel at the truncated lightmap coordinates, all its styles summed at their current brightness) times the reflectivity of that surface's material. A material's reflectivity is the VTF header's reflectivity of its first texture among `$basetexture`, `$envmapmask`, `$bumpmap`, `$dudvmap` and `$normalmap` (the same values vbsp stores per texdata). Each cube face is the average of the rays on its side, weighted by the cosine to the face's axis.
- **World lights**: every type except the sky ambient. A light is considered if its cluster is in the point's PVS, its attenuation at the point is above zero (point and spot: 1 / (constant + linear × d + quadratic × d²), cut off beyond the radius when there is one; emissive surfaces: 1 / d² with d² at least 1, also cut at the radius), its brightest channel times that attenuation is above `r_worldlightmin`, and a trace from the point towards it, stopping 8 units short of the light, is clear. The sun counts once, with factor 1, where a ray against its direction meets the sky.
- **Light styles**: the state is built in two parts. Lights of style 0 are taken at full brightness when the state is first needed. Lights with any other style are listed when the state is created (those whose cluster is in the point's PVS) and added on top of the cached style-0 part through the same selection, at their style's current brightness (the style's value over 264, 'm' being 1); the state remembers which styles it depends on and redoes that styled part when one of them changes (the style-0 part and the indirect light are kept). So a flickering or switched light changes which lights are local and what the cube holds, as it changes the lightmaps.
- **Choosing the local lights**: each considered light gets a brightness, the luminance of its intensity times its attenuation, without the spot cone. Lights at least `r_worldlightmin` bright fill the local-light slots; when the slots are full, a brighter light replaces the dimmest one, so the brightest two are kept. Every light not kept goes into the cube, spread over the faces by the cosine to each axis and multiplied by its spot cone (or, for emissive surfaces, by the cosine at the emitter). The sun goes into the cube with no direction, so it adds nothing there: it lights a model only as a local light.
- **Local lights** are passed on as: point lights with their three attenuation terms; spot lights with the outer cone kept between 80 degrees and 0.99 in cosine and the inner cone at least 0.001 tighter, and the spot exponent (0 meaning 1); emissive surfaces as spot lights along their normal with an 80-degree cone, a linear falloff and 1 / d² attenuation; the sun as a directional light. Each also gets a range: its radius, or, without one, the distance at which the length of its intensity over its attenuation falls to 0.03.

**The static-prop bake** (`CModelRender::UpdateStaticPropColorData`, run when the map loads, behind `r_staticlighting`, default 1): for every vertex of every static prop, the engine takes that prop's lighting state with its style-0 part only and evaluates `StudioRender`'s lighting function at the vertex's world position and normal: the cube weighted by the squared normal components, plus each local light's colour × max(n·l, 0) × its attenuation × its spot ramp, with nothing beyond the light's range and no visibility test per vertex. The sum goes through the gamma table (index = light × 1024, entry (i / 1024)^(1/2.2) × ½ for overbright 2, at most 1), is clamped to 0–1 per channel and stored as an 8-bit vertex colour. The shader's baked-colour combination would undo the encoding (× 2, then ^2.2). **The bake is not drawn in this build**, though: static props are drawn like other models (`CStaticProp::DrawModel` hands its light cache to `CModelRender::DrawModel`, which calls `RenderModel`), and `RenderModel` always asks for the whole lighting state (style-0 lights, styled lights and dynamic lights) and passes no colour meshes to `StudioRender` (the field of its draw info that `StudioRender` hands down as each mesh's colour stream is always null). So static props are lit by the shader from the same state as every other model, styled lights included.

**OpenBL's model** follows that state: one sample per model at its lighting origin (props: the static prop's lighting origin, else the model's centre; NPCs: chest height), the same indirect gather with each hit surface's texdata reflectivity, the same choice of local lights, and the cube for the rest. The two local lights are evaluated per vertex in the shader (per pixel with a normal map), with the same ramps and cones and a hard cut-off at the range. Lights with a style take part at the style's current brightness: each model keeps the lights it gathered when the map loaded, without their styles applied, and chooses its local lights and cube again whenever one of its styles changes value (`PropRenderer::update_lighting`, `CharacterRenderer::update_lighting`; with `--no-light-styles`, only style 0). Next to `sm_hub_1`'s fluorescent flicker (style 10, by the Asylum) the caged lamp beside it now flickers with the wall's lightmap. Differences that remain:

- The indirect light is gathered once, with every lightmap style at its default brightness; the original also builds it once, with the styles' brightness at that moment (a style change redoes only the styled part).
- No PVS test before the line of sight, and every light's attenuation factor is capped at 1.
- **Embedded props** (centre inside solid): each side of the cube is sampled from just outside that side of the model's bounds, with all lights folded in.
- The shader's range term w is not known; OpenBL cuts each local light off at its range, as the bake does (and the `sm_pier_1` capture agrees).

**Checked against captures** (original ÷ OpenBL in linear terms; geometric mean over prop regions, red / green / blue, with the spread in natural-log terms). `sm_pier_1`'s arcade front (camera 4936.16 −813.63 −168.1, angles 4.70 / 89.48; lamps hang on the facade and three blue spotlights light it from below), 150 blocks of 40 × 40 pixels on the props: 1.01 / 1.01 / 1.00 with a spread of 0.13 / 0.20 / 0.20 (block medians; block means give 1.00 / 1.00 / 0.99). The previous model, which ranked lights by what reached the sample point (so the spotlights aimed past the facade's centre were dropped), kept lights inside a model's bounds in the cube and weighted the indirect light by a fitted 0.15, gave 1.41 / 1.50 / 1.53 with a spread of 0.36 / 0.47 / 0.51. The ranking without the cone did most of it (0.88 / 0.88 / 0.86), the range cut-off and the cone limits the rest. `sp_tutorial_1`'s office (two captures, 10 prop regions below the ceiling fan): 0.87 / 1.02 / 1.26 by region medians and 1.10 / 1.16 / 1.16 by region means (the previous model: 1.00 / 1.00 / 0.90 and 1.16 / 1.12 / 1.01). The office's props are dark, so their medians move in steps of one 8-bit level (10–40 %); the means are the steadier measure there. On `sm_hub_1` the NPC nearest the barrel fire is now lit by it (its light sits inside the barrel's bounds), and the 3D skybox's parking structure (which an earlier version turned black by lighting it per vertex from the lights inside it) is lit normally. Still open: captures from more rooms, and a capture of a model beside a flickering light.

Self-illumination blends to base × tint by base alpha, as for the world. Alpha-tested models discard pixels with a texture-kill instruction driven by a separate coordinate.

**Environment maps on models** (1,071 `VertexLitGeneric` materials) are added the same way as on the world (the `v2` model shaders add envmap × mask × tint after lighting). A model with `$envmap env_cubemap` uses the map cubemap nearest to it: its lighting origin for props, its chest height for NPCs. Checked on `sm_hub_1`'s alley manhole (a prop, station 4 in [PROXY_CAPTURE.md](PROXY_CAPTURE.md)): mean colour 14.8 13.5 9.6 in the original, 13.9 12.4 9.6 in OpenBL, and 4.3 1.8 0.2 without the envmap. On characters it is mostly small parts, such as the police officers' sunglasses.

### Normal maps on models

94 `VertexLitGeneric` materials have a `$bumpmap`, 57 of them with an `$envmap`. They are mostly structural scenery props, monsters, furniture and weapons, plus a few characters (VV's body, the Chang brothers). The original draws them with `vertexlit_and_unlit_generic_bump`, which lights per pixel:

- the vertex shader passes the tangent, binormal = cross(normal, tangent) × w, and normal, all in world space. Every model stores a tangent per vertex ([formats/models.md](formats/models.md));
- the pixel shader takes the normal map's texel × 2 − 1 in that frame, normalised (through a normalisation cube map);
- it evaluates the ambient cube with that normal: the squared components weight the +/− face of each axis;
- it adds up to two local lights, each max(n·l, 0) times a colour from the vertex shader. As in the plain shader, `$halflambert` would switch to (n·l × 0.5 + 0.5)², but no material sets it;
- if the sum's largest channel exceeds 2^2.2 (4.59479), it scales the whole colour down to it, which keeps the hue. Only the bump-mapped shader clamps;
- the result times `$color` and the base texture; the envmap is reflected about the bumped normal and masked by the normal map's alpha, before tint and contrast, as for the world.

OpenBL evaluates the ambient cube and the two local lights (above) per pixel with the bumped normal, and reflects the envmap as the original does. It leaves out the clamp: in linear terms it would start at a light level of about 28 (4.59^2.2), far above anything seen. The compiled tangents are not always unit length or perpendicular to the normal, and 9.5% of all vertices have none usable (zero, where the texture coordinates collapse). OpenBL makes them perpendicular and keeps the plain normal where nothing is left. Characters skin the tangent like the normal.

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

## Ropes and cables

`keyframe_rope` and `move_rope` entities (2,448 over all maps; 99 in `sm_hub_1`, making 76 ropes). Each one with a `NextKey` hangs a rope from its own origin to that entity's, with its own `Width`, `Slack`, `Subdiv`, `TextureScale` and `RopeMaterial`. Chains of keyframes (telephone lines from pole to pole) are one rope per span.

**The `Cable` shader** (8 materials; `vsh/cable.vcs` and `psh/cable.vcs`, read from the shipped files). The engine supplies two colours per vertex: the lighting where the rope is, and a light direction expressed in a frame built from the rope's direction and the strip's side direction. The pixel shader multiplies the base texture by that lighting and by the normal map dotted with the light direction, plus a constant. The base texture's u runs across the rope and v along it, and the normal maps (`cable/cablenormalmap`, 16 × 4) shade it as a round cable. The lighting is clamped to `$minlight` and `$maxlight`; the clamping happens in engine code, so its exact form is not known. Some rope materials are `UnlitGeneric` instead (Christmas lights, alpha-tested).

**OpenBL**:

- **Shape**: a chain of `Subdiv` + 2 nodes (`Subdiv` 0 to 8) with equal links, hanging at rest under gravity (`anim::rope_rest_nodes`, solved by relaxation), drawn as a Catmull-Rom curve through the nodes, about one point every 12 units.
- **Slack**: the chain's length is the straight distance plus **0.15 × `Slack`**. With the whole `Slack`, the telephone cables on `sm_hub_1` (spans of about 800 to 1,000 units, `Slack` 70 to 80) hang about 170 units below their poles: into the traffic-light view (station `j1` in [Sprites](#sprites-env_sprite)), where the original shows no cables, and in deep curves across station `c1`, where the original's run nearly straight to the pole. Fitted on `c1`'s capture (the original's cables isolated by its `r_drawsprites` on/off pair, which also hides cables): the original's lines are a median 3.6 pixels from OpenBL's at 0.15, and 5–10 pixels at 0.1 or 0.2. The cables in view there belong to five-span chains, so spreading one `Slack` over a whole chain (0.2 per span) would fit about as well; no capture yet shows a single-span rope.
- **Strip**: `Width` units wide, turned about the rope's direction to face the camera. v advances by 4 texels per unit at `TextureScale` 1, and larger scales stretch the texture (the Valve Developer Community's `move_rope` page). Chains, caution tape and Christmas lights then come out about 1.3 times longer than square.
- **Light**: at each node, the mean of the six faces of the prop-lighting ambient cube with all lights folded in ([Models](#models-vertexlitgeneric)), encoded (^(1/2.2)) and clamped per channel to `$minlight`–`$maxlight`, interpolated along the rope. The normal map's light direction is taken as the strip's facing, so the shading term is the normal map's z. The engine's light direction and constant are not known. Unlit materials take the base texture alone.
- `Type`, `Collide`, `Barbed`, and ropes moving between keyframes are not modelled. Ropes are not culled. `openbl-viewer --no-ropes` hides them.

## Translucent order

The original draws translucent things in BSP order, back to front from the eye, as Source does:

- **World surfaces.** A face lying on a BSP node is drawn between the node's far and near sides. Detail faces lie on no node (7,380 of sm_hub_1's 11,906 world faces); each is drawn in the nearest leaf whose face list holds it.
- **Entities** (props, characters, sprites, brush entities) are registered in every leaf their bounds touch and drawn in the nearest one. In a leaf, the world surfaces come first, then the entities, farthest first.

OpenBL follows this with one queue per view (`render/translucent.hpp`, `formats/draw_order.hpp`): each renderer lists its translucent items with their BSP slot and distance, and the queue draws them in order. Water comes first. A particle effect is one item, placed by the bounds of its current particles. Sprite bounds are half the picture's larger side around the origin; props use their model bounds, characters a 32 × 32 × 72 box. With the skyline light cones of `sm_hub_1`, placing sprites by their origin alone put the skybox's translucent building facades over them (0.60 of the original's cone light); with their bounds the cones measure 0.87 as before.

## Detail props

The `dprp` game lump ([formats/maps.md](formats/maps.md#detail-props-dprp-v2-dplt)) places small models across the outdoor maps: 134,490 in all, up to 25,434 on one map (`sp_observatory_1`). This version has no detail sprites: every entry is a model, mostly `grassa`/`grassb` (12 triangles each), weeds and pebbles. Each stores one light colour (`ColorRGBExp32`), plus optional extra light styles in `dplt`.

**Materials.** Every detail model's material is `UnlitGeneric`, even the pebbles (`rocksmall`), and the grass and weeds are `$translucent`. Their textures are mid-bright (`grassa`'s opaque texels average 110 / 120 / 78), so unmodulated they would glow at night. The engine must therefore multiply them by the stored colour. OpenBL treats that colour like a luxel: lit colour = base × light in linear terms, as for the world. The extra styles are added at their default brightness. On night maps much of the grass has no light at all (more than half of `sp_observatory_1`'s, a map without sky ambient light), and it draws black.

**OpenBL.** When the map loads, every prop's mesh is transformed into world space with its light per vertex, merged per material, and grouped by the cluster of its leaf (`DetailPropRenderer`). The groups are culled like the world ([Visibility](#visibility)). Opaque materials draw first. Translucent ones are blended without depth writes, after the opaque world, props and characters, their cluster groups from far to near. Unlike the original's per-item translucent queue, nothing is sorted within a group.

Not known: the original's draw distance and fade for detail props. OpenBL draws them at any distance. The cost is within measurement noise (about 2 ms per frame with or without them on `sp_observatory_1` and `hw_cemetery_1` at 1600 × 900). `openbl-viewer --no-detail-props` hides them.

## Character shadows

Bloodlines replaces Source's single top-down shadow with its own settings: `cl_v_shadows` (0 none, 1 "Simple", 3 "Soft"; the shipped `config.cfg` has 3) and `cl_v_shadow_count`, "the number of shadows drawn per character". The materials are Source's render-to-texture ones: `engine/shadowbuild` draws a model's silhouette (`ShadowBuild`, `$nocull`), and `engine/rendershadow` projects it, with the `Shadow` shader's lerp(1, shadow colour, shadow alpha) multiplied into what is behind it. Its DX8 help text adds `$falloffamount` 0.9, "amount to brighten the shadow at max dist".

**Observed in the original** (GOG 1.2 without the Unofficial Patch, windowed, each view grabbed with `cl_v_shadows` 3 and 0 and divided pixel by pixel):

- **Casters**: characters only, one shadow each. NPCs cast; so does the player in third person, unless in noclip.
- **Receivers**: the world only. The console reports that `engine/rendermodelshadow`'s `ShadowModel` proxy fails to initialise, so no shadow falls on a model.
- **The light**: the brightest light at the character. Beside `sm_hub_1`'s barrel fire (`light_spot` at 772 −2321 −83, intensity 1000) the bums' shadows point away from the fire. With the player stepped away from it along y = −2320, the shadow follows the street light at 585 −2345 15 (intensity 2000) once that light is stronger: pointing towards the fire at 100 and 140 units from it, under the feet directly below the street light, and west once past it. In the `sp_tutorial_1` office, whose lamps reach the player only weakly, there is no shadow at all.
- **Shape**: a short, soft-edged silhouette at the feet, about shoulder-wide and a little longer, offset away from the light. Even the fire, lower than the bums' chests, puts the shadow on the ground beside the feet, so the projection is not from the light's position.
- **Strength**: the core keeps 0.28 / 0.37 / 0.41 of the encoded colour (R / G / B; two bums, about 150 units from the camera, where `sm_hub_1`'s fog has not started), a bluish shadow colour of about (73, 96, 106). The darker floor behind the barrel gave 0.24 / 0.30 / 0.33, the same hue.
- **Count**: at runtime `cl_v_shadow_count` reads 1 (default 2). Setting it to 2 or more, or `cl_v_shadows` to 1 or 2, while a map is running removes the shadows instead of adding more, so only one shadow per character has been seen.

**OpenBL** (`CharacterRenderer`, `shaders/shadow.hlsl`):

- **The light** is the brightest of the character's local lights ([Models](#models-vertexlitgeneric)) at its lighting origin, if it arrives with a luminance of at least 0.25 in lightmap units. The left bum at the fire (0.55) casts one, as in the original; the office lamps reach the player with less than about 0.1. The cut-off is fitted to these two cases only.
- **Direction**: from the light through the character's centre (36 units up), or along a directional light, turned to point down and tilted from vertical by at most 20°. 45° and 30° gave long streaks; 20° gives the original's compact blob (5,800 shadow pixels against its 4,500 for the same bum and camera).
- **Silhouette**: each frame, for the shadows the main view can see, the character's opaque meshes are drawn two-sided (alpha-tested ones clipped) into its own 128 × 128 render target with an orthographic projection along that direction, 88 units square.
- **Receivers**: the world's faces in the leaves the shadow can reach, and displacements, that are opaque, lit and face the light. Their triangles are gathered once when the map loads (characters do not move yet) and drawn after the world's opaque surfaces and decals, before props and characters cover them. Each pixel takes the silhouette's coverage (nine taps for the soft edge) times 1 − 0.9 t, where t runs from 0 at the far side of the character's box to 1 at 150 units past it, and multiplies by lerp(1, (0.285, 0.375, 0.415), coverage) on encoded values, fading to neutral in fog. Measured on the fire bum, OpenBL's shadow keeps 0.35 / 0.43 / 0.47 (median) against the original's 0.28 / 0.37 / 0.40; the difference is mostly OpenBL's softer rim.
- The cost is within noise: 2.61 against 2.59 ms per frame on `sm_hub_1`'s street at 1600 × 900.

Not done: "Simple" blob shadows and more than one shadow per character (neither seen working in the original), how the original picks the light exactly, shadows of moving characters (Phase 2), and props. `openbl-viewer --no-shadows` turns them off.

## Visibility

Each view draws only what it can see (`formats::ViewVisibility`). That takes three tests:

- **The PVS**: the clusters in the potentially visible set of the eye's cluster (the map's visibility lump).
- **Area portals**: the clusters in an area reachable from the eye's area through open portals (the map's areas and area portals lumps).
- **The frustum**: the view's left, right, bottom and top planes, from its view-projection matrix.

An eye in solid or outside the visibility data sees everything.

**Area portals.** 518 portal entities over all maps: 237 `func_areaportal` and 281 `func_areaportalwindow`. 224 of the `func_areaportal`s have `StartOpen` 0, and nearly all of those name a door (`func_door_rotating`, `func_door`) or a `func_brush` as their target: the portal closes the area behind a closed door. OpenBL draws doors closed (entity I/O comes in Phase 2), so those portals start closed; the rest, with `StartOpen` 1 or no key (open, the FGD default per the Valve Developer Community), and every `func_areaportalwindow` is open. A `func_areaportalwindow` draws its `target` brush (a `func_brush`) itself, faded by the viewer's distance to the brush's bounds: at the `TranslucencyLimit` opacity (0 in every shipped map) up to `FadeStartDist`, rising linearly to opaque at `FadeDist`, as Source's `C_FuncAreaPortalWindow` does. OpenBL draws that brush in the translucent queue with that opacity (opaque materials fade by it alone). Drawn opaque, it hid the inside of `sm_pier_1`'s arcade behind a black doorway; with the fade the doorway matches a capture of the original (camera 4936.16 −813.63 −168.1, angles 4.70 / 89.48, about 220 units from the brush: opacity 0.24). Beyond `FadeDist` the portal could close for culling; it stays open. A portal's `portalnumber` is the key of its two area-portal records. `ViewVisibility::set_portal_open` changes one, for scripts later. Leaves in area 0 (solid, outside the map) are not affected.

- **World**: each opaque material batch keeps its faces grouped by the clusters they lie in: those of the leaves listing the face, or, for brush entities and displacements, those of the leaves the face's bounds touch. Each group keeps its bounds. A view draws the groups with a cluster in its PVS and bounds in its frustum, and adjacent groups merge into one draw. Decals are grouped the same way, by the clusters of the faces they lie on. Translucent faces are culled as one item per batch, by the leaves their bounds touch and the bounds. Water is not culled.
- **Props, characters, particle effects**: drawn if one of the leaves their box touches is in the PVS and the box is in the frustum. **Sprites**: their leaves only.
- **Views**: the 3D skybox uses the sky camera's eye, and monitors their camera's. The water reflection uses the real eye's PVS, since the mirrored eye is often inside solid.
- **Characters** outside the main camera's PVS are not animated. Their cloth settles again when they come back, as on the first frame.

Culled and unculled renders are pixel-identical at a fixed time (`--time`) on five views of `sm_hub_1`, `sp_tutorial_1` and `ch_hub_1`, and, with area portals, on four views each from the player start of `sp_giovanni_2a`, `sp_giovanni_2b`, `ch_temple_1` and `sp_tutorial_1` (the maps with the most closed portals). One view of `sm_warehouse_1` differs by 274 faint pixels, as it already did before area portals and decal culling; not yet explained. Area portals take the `sp_giovanni_2a` start view from 2.00 to 1.37 ms per frame. `openbl-viewer --no-area-portals` opens every portal. Frame times at 1600 × 900 on an RTX 4060 laptop, measured as the slope between 300 and 1,200 frames: `sm_hub_1` 1.8–2.8 ms against 7.2–11.4 ms without culling, `sp_tutorial_1` 0.2 ms against 2.2 ms. `openbl-viewer --no-cull` draws everything.

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

The colours combine on encoded values, as in the original. Materials without `$refracttexture` show their fog colour instead, as does all water with `openbl-viewer --no-water`. Water and monitors are left out of both targets. Only the upward-facing water faces are drawn; the few others are the sides of water volumes. Across the `ch_lotus_1` pool at 1600 × 900, the two passes take a release build from about 6 to about 8 ms per frame (a debug build from 19 to about 29 ms).

**Checked** on `ch_lotus_1` (station 10 in [PROXY_CAPTURE.md](PROXY_CAPTURE.md), looking down into the pool): the floor shows through and warps as much as in the original. The warp amplitude, estimated as temporal change ÷ spatial gradient over 24–60 frames, is 3.7 px against the original's 3.8 px on lightly blurred frames, and 5.6 against 6.8 px on more blurred ones. The original's warp breaks into horizontal streaks a few pixels tall, which OpenBL does not reproduce; they look like how the hardware samples the signed dudv texture. Colours over the pool are within 10% except blue (18 against 22 of 255), but the stone rim beside the pool is about 30% darker in OpenBL as well, so that difference is the room's lighting, not the water.

**Reflections differ, deliberately.** From a level view across the same pool (camera 760.13 0.13 −194.94), the original's far half of the pool is almost black: the refraction fogs out along the long underwater ray, and no reflection of the red beams or lanterns shows. OpenBL reflects them clearly at that angle. Whether the original drops the reflection there (blend state, cheap water, or the shader's constants) is not settled. OpenBL keeps its reflections as an improvement rather than matching the black water.

**Cheap water.** The original's `watercheap_ps20_old` draws `$fogcolor` plus the water's `$envmap`, sampled with the eye vector reflected about the normal-mapped normal and weighted by (1 − cos)² (not the expensive shader's fourth power), with no refraction or tint. OpenBL blends from expensive to cheap water between `$cheapwaterstartdistance` and `$cheapwaterenddistance` from the eye, per pixel (the help text in the shader DLL calls this a transition). `$forcecheap` water is cheap everywhere. In the 3D skybox, the distances are divided by the skybox scale, as for its fog, and cheap water needs no render targets. So the open sea around `la_dane_1` (skybox water, `dev_waterbeneath2`, cheap from 500 to 1000 units) now shows the cubemap's waves instead of flat fog colour.

**From below.** The map has no separate underside faces: the downward-facing faces with the `$bottommaterial` that the map compiler is given are not in the BSP (in `hw_warrens_4` the faces with `dev/dev_waterbeneath2` face up too). The engine draws the same surface from below with the material its `$bottommaterial` names: 19 shipped water materials name `dev/dev_waterbeneath2`, a `Water` material whose bottom is itself. In `hw_warrens_4` and `ch_temple_2` the map pakfile replaces it with a `WaterWarp` material. `WaterWarp` is not a separate pixel shader: the DX8 shader DLL lists the name beside `Water_Old_dx80_dx81_dx90`, and the only `WaterWarp` program shipped is a DX8 vertex shader (`vsh/waterwarp_old.vcs`) that builds the same screen-space distortion as the water refraction. OpenBL draws it as water.

Captured in the original (`hw_warrens_4`, the eye 22 units under the surface at −1184 −4736 −2425): looking up, the surface shows a strongly warped picture of the tunnel above, with the waterline a dark wavy edge; looking level, the floor and walls under water are only lightly fogged, and the surface overhead at a grazing angle continues the wall below.

OpenBL, when the eye is below a water plane:

- **The surface** is drawn with its `$bottommaterial`, with the normal pointing down. The refraction target holds what is above the plane (no water fog); the reflection target holds the scene below the plane, mirrored, in the water's fog (the whole mirrored path is under water). The Fresnel weight (1 − cos)⁴ makes it reflect at grazing angles.
- **Underwater fog.** An eye in a water leaf (BSP contents) sees everything through that water's fog (`$fogcolor`, `$fogstart`, `$fogend`), counting only the part of each view ray below the surface: the water fog formula above, extended to an eye below the surface.
- Against the captures, the structure matches (the warped underside, the wavy waterline, the reflected wall at grazing angles). OpenBL is darker: 0.86 of the original's brightness above water in this map (the Warrens' bump-mapped lighting), 0.78 for the floor seen under water, and about 0.6 for the surface seen from below. Not yet explained.

**Not done:** expensive water in the 3D skybox (no targets there; it shows its fog colour until cheap water takes over; the shipped skybox sea, `la_dane_1`'s, turns cheap from 500 / 16 units).

## Still to read

Eyes, the envmap and mask variants, and the compiled vertex shaders, which hold the vertex lighting.
