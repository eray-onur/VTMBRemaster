# Material proxies

Proxies are small per-frame programs listed in a material's `Proxies` block. They compute values and write them into the material's parameters: they animate textures, scroll UVs, make lights flicker, and make surfaces wet in the rain. The Steam release uses 26 kinds; [shaders.md](shaders.md) lists every kind with its keys, use counts and shaders.

**How sure this is:** the keys and how they are used come from the shipped materials. The behaviour below follows the public description of Source-engine material proxies and the names of the keys. None of it has been checked against the original game yet. Proxies marked *Bloodlines* are not part of the stock engine; their behaviour is inferred from their names and the materials that use them.

## How proxies work

- **Material variables.** Every `$name` in a material is a variable. Materials also declare scratch variables purely for proxies, such as `$temp`, `$one`, `$zero` and `$a_b_noise`. That is why such names appear as "parameters" in the catalogue.
- **Inputs** (`srcVar1`, `mean`, `halfwidth`, …) are variable names. Some keys (`sineperiod`, `rate`, `scale`, `minVal`) take literal numbers.
- **Outputs** (`resultVar`) name a variable, optionally with a component index: `$color[0]` writes only the red channel, `$envmaptint[0]` only the first component.
- Proxies run in the order listed, every frame the material is drawn, so one proxy's result can feed the next. `dev_tvmonitor1a` chains `GaussianNoise`, `LessOrEqual`, `Subtract`, `Abs` and `Exponential` into a flickering TV picture.

## Reference

| Proxy | Keys | Behaviour |
|---|---|---|
| `Sine` | `resultVar`, `sinePeriod`, `sineMin`, `sineMax`, `timeOffset` | Sine wave between min and max over the period in seconds. Five materials spell the key `“timeoffset”` with smart quotes, so that key is not recognised. |
| `LinearRamp` | `resultVar`, `rate`, `initialValue` | Value increasing at `rate` per second. |
| `UniformNoise` | `resultVar`, `minVal`, `maxVal` | Random value each frame. |
| `GaussianNoise` | `resultVar`, `mean`, `halfWidth` | Normally distributed random value. |
| `Add`, `Subtract`, `Multiply` | `srcVar1`, `srcVar2`, `resultVar` | Arithmetic. |
| `Abs`, `Exponential` | `srcVar1`, `resultVar` | Absolute value, e to the power. |
| `LessOrEqual` | `srcVar1`, `srcVar2`, `lessEqualVar`, `greaterVar`, `resultVar` | `resultVar = srcVar1 <= srcVar2 ? lessEqualVar : greaterVar`. |
| `AnimatedTexture` | `animatedTextureVar`, `animatedTextureFrameNumVar`, `animatedTextureFrameRate`, `animationNoWrap` | Steps the frame variable through the texture's frames at the frame rate (e.g. `$bumpframe` of an animated water normal map). |
| `TextureScroll` | `textureScrollVar`, `textureScrollRate`, `textureScrollAngle` | Builds a translation that scrolls at `rate` texture-widths per second in the given direction, written to a transform such as `$baseTextureTransform`. |
| `TextureTransform` | `translateVar`, `rotateVar`, `resultVar` | Builds a texture transform from variables. |
| `WaterLOD` | `dummy` | Selects cheap or expensive water. A comment in `dev_water2.vmt` says the dummy key "has to be here, or material loading barfs". |
| `PlayerPosition`, `PlayerSpeed` | `resultVar`, `scale` | Player position or speed times `scale`. Only test materials use them. |
| `PlayerProximity` | `resultVar`, `scale` | Distance from the player to the entity, times `scale`. Used by street-light sprites (`streetlight3_proxyfade`) to fade with distance. |
| `GlobalWetness` *(Bloodlines)* | `resultVar`, `scale` | A global wetness level, presumably driven by rain, times `scale`. 135 world materials write it into `$envmaptint` so that surfaces get shinier when wet. |
| `LampBeam`, `LampHalo` *(Bloodlines)* | — | Fading for street-light beam and halo sprites. |
| `TextConsole` *(Bloodlines)* | — | Computer screens (`models/scenery/furniture/computer/screen`); probably draws the hacking-terminal text. |
| `Camo` | `camoPatternTexture`, `camoBoundingBoxMin/Max`, `surfaceprop` | A camouflage effect inherited from the engine; one development material. |
| `BreakableSurface` | — | Breakable glass: selects the cracked texture state. |
| `Shadow`, `ShadowModel` | — | Engine render-to-texture shadows. |
| `ParticleSphereProxy` | `direction` | Lighting direction for particle spheres. |
