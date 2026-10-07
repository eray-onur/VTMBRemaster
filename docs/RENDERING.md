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
- **Gamma.** The original multiplies in pure gamma 2.2 space; OpenBL multiplies in linear space with sRGB textures and output. The two agree except in dark tones, where the sRGB curve's linear toe makes OpenBL up to about 15% darker (seen in the blue channel of the brown `sp_tutorial_1` office).
- **Self-illumination**: the result blends from the lit colour to base × self-illumination tint, by the base texture's alpha.
- **Detail textures** (`$detail`): multiplied in at 2× (a mid-grey detail texture leaves the colour unchanged), before the lightmap doubling.
- **Environment maps** (`$envmap`): the envmap × `$envmaptint` is added to the base before the lightmap is applied, so reflections are lit by the lightmap too.
- **Translucent lightmapped surfaces**: base × lightmap × 2, with alpha from the base texture.

## Two-texture blends (`WorldVertexTransition`)

The blend factor is not a vertex attribute. It is stored in the **lightmap texture's alpha**, and the shader uses one minus it: the second base texture shows where that alpha is 0. The result is then lit like other world surfaces. OpenBL keeps the displacement alpha per vertex. That gives the same blend if the engine writes 1 − alpha/255 into the lightmap alpha, which is unverified.

## Bump-mapped lightmaps

Bump-mapped faces have three lightmaps, one per basis direction in tangent space (stored as 4 per style in the BSP; see [formats/maps.md](formats/maps.md)). The shader dots the normal-map normal with each of the three basis axes (saturated at 0) and sums the three lightmaps weighted by those dot products.

## Models (`VertexLitGeneric`)

Lit colour = base texture × colour modulation (`$color`, alpha) × per-vertex lighting × 2 × (overbright ÷ 2). The per-vertex lighting is computed by vertex shaders that are only shipped compiled. How it is scaled has not been worked out. OpenBL scales its estimated ambient cube by a provisional 2.297; against two captures of `sp_tutorial_1`, props then average 1.16 / 1.10 / 0.89 of OpenBL's brightness in the original (red / green / blue linear), so OpenBL's props are slightly dark and too blue. The scale may stay, but the ambient term itself is still an estimate. Self-illumination blends to base × tint by base alpha, as for the world. Alpha-tested models discard pixels with a texture-kill instruction driven by a separate coordinate.

## Unlit surfaces (`UnlitGeneric`)

Base texture × vertex colour, with no doubling.

## Still to read

Water (`waterreflect.psh`, `waterrefract.psh`), eyes, the envmap and mask variants, and the compiled vertex shaders, which hold the vertex lighting and fog.
