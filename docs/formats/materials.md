# Materials (`.vmt`) and KeyValues text

A material names a **shader** and sets its **parameters**, most importantly which textures it uses. Materials are text files in the KeyValues syntax. Many other text files in the game use the same syntax.

Implementation: `src/formats/keyvalues.cpp` (syntax) and `src/formats/vmt.cpp` (materials).

## Syntax

```
// comment to end of line
"VertexLitGeneric"
{
    "$basetexture"  "models\scenery\misc\fwheel\whllghts"
    $translucent 1
    "Proxies"
    {
        "AnimatedTexture"
        {
            "animatedTextureVar" "$basetexture"
            "animatedTextureFrameRate" "10"
        }
    }
}
```

- A document is a sequence of `key value` pairs and `key { ... }` blocks.
- **Quoted strings** run to the next `"`, and may span lines.
- **Unquoted tokens** end at whitespace, `"`, `{` or `}`.
- **There are no escape sequences.** Backslashes are path separators in 4,981 of the 11,052 materials. Treating `\t` in `plaster\twrbllceila` as a tab would break those paths.
- Keys are compared case-insensitively.
- All files use CRLF line endings. Text is Windows-1252 and is handled as raw bytes.

## Materials

The first top-level block is the material. Its key is the shader name, and its children are:

- **parameters** (`$basetexture`, `%keywords`, …): leaf pairs. If a parameter appears more than once, **the last value wins**. This is an assumption still to be verified: 208 materials repeat a key, 55 of them with different values, for example `$surfaceprop` set to both `concrete` and `brick`.
- **`Proxies`**: a block of named proxies that drive parameters at runtime (`Sine`, `AnimatedTexture`, `TextureScroll`, …).
- **Other blocks**: only `LightmappedGeneric_DX6`, in 5 materials. These are fallbacks for old hardware.

Texture parameters are relative to `materials/`. A value may use `\` or `/`, may start with `/`, and may include an extension. `materials/` + the normalized value + `.tth` gives the texture.

### Colours and water

Colour parameters use two notations: `{r g b}` in 0–255 and `[r g b]` in 0–1. So `"$fogcolor" "{.4 .4 .2}"` (in `water/warrenwater`) really is almost black. OpenBL keeps this behaviour.

`Water` materials have no base texture. They use `$normalmap`, `$fogcolor`, `$reflecttint`, reflection and refraction amounts, and screen render targets (`_rt_WaterReflection`, `_rt_WaterRefraction`). Water that only defines a gameplay volume, such as `water/invisible_water` on the pier, is `UnlitGeneric` with `%compilenodraw`, and is never drawn.

### Usage in the Steam release

There are 11,052 materials using 52 shaders. The most common are `VertexLitGeneric` (5,250), `LightmappedGeneric` (3,468), `UnlitGeneric` (1,617), `Eyes` (391), `WorldVertexTransition` (87), `Sprite` (61), `DecalModulate` (38) and `Water` (24).

197 materials have proxies, of 26 kinds. The most common are `Sine` (114), `AnimatedTexture` (65), `TextureScroll` (54) and `GaussianNoise` (25). `GlobalWetness`, `PlayerSpeed` and `Camo` look specific to Bloodlines.

## Damaged and odd materials

The parser never fails on bad syntax. It recovers, and records a diagnostic with a line number. 157 materials produce diagnostics:

| Problem | Count | Handling |
|---|---|---|
| Block never closed (missing final `}`) | 27 | Closed at end of file |
| Extra `}` after the material | 15 | Ignored |
| `\\` typed instead of `//` to comment out a line | 125 lines | **Not a comment.** `\\` becomes a key, and the pairing of the following tokens shifts by one. Example: `lightsuperoff.vmt` ends up with a parameter named `\\` whose value is `$selfillum`, so `$selfillum` is never set. |
| Windows smart quotes `“ ”` instead of `"` | 5 | Treated as ordinary characters, so `“metal”` is the literal value, quotes included |
| Block opened with `}` instead of `{` | 1 | `materials/models/gibs/glass-gib.vmt` has no shader block and does not load |

The handling of the `\\` lines and the smart quotes follows from the syntax rules above. It probably matches the original engine, so those parameters really were lost in the original game.

To do: confirm in the original game, for example check whether `lightsuperoff` glows.

**Missing textures**: 115 of 10,913 `$basetexture` references point to textures that do not exist. Most are typos or files that were moved (`sprites/richo1` for `sprites/rico1`, and `doppleganger` paths). Two are `_rt_` render-target names, which the engine creates at runtime instead of loading from disk.
