# Parity testing

OpenBL aims to match the original game, not merely look plausible. Parity tests compare screenshots from fixed camera positions, taken in the original game and in OpenBL, and report numbers rather than impressions.

## Tools

**Shot lists** are plain text files with one camera per line: `name x y z pitch yaw`. Lines starting with `#` are comments.

```
# sm_pier_1
pier_road   3700 -700 -230   -5   0
pier_cop    3820 -742 -240   -8 180
```

**Rendering a shot list in OpenBL.** This writes `<out>/<name>.png` for every line:

```sh
openbl-viewer --map sm_pier_1 --shots pier_shots.txt --out shots/openbl --size 1600x900 --fov 75
```

**Comparing two images** (PNG or TGA, same size). This reports, per channel, the mean brightness of the reference and the candidate and their ratio, plus the mean absolute difference and the PSNR, for the whole image or for a region `x y w h`:

```sh
openbl-cli image compare shots/original/pier_road.png shots/openbl/pier_road.png
openbl-cli image compare shots/original/pier_road.png shots/openbl/pier_road.png 600 300 400 300
```

A brightness ratio close to 1.0 across many shots confirms the lighting. The world lightmap scale was measured this way (see "Results").

## Capturing reference shots in the original game

Verified on the GOG release (1.2, without the Unofficial Patch), 2026-10-07.

1. Start the game windowed with the console: `Vampire.exe -window -w 1280 -h 720 -console`. The console starts open.
2. In the console: `map sp_tutorial_1`, `noclip` (a toggle), `cl_showpos 1`. The overlay shows the player's feet (`plr pos`), the camera (`cam pos`, 64 units above the feet) and the view angles (`cam ang`).
3. Bloodlines has no `setpos`, `setang` or `getpos`. The console runs unknown commands as Python, so this teleports the player (feet position):
   ```
   __main__.FindPlayer().SetOrigin((263, 200, 90))
   ```
   `SetAngles` does not turn the view; turn with the mouse and read the angles from the overlay.
4. Grab the window's client area with any screen-capture tool, then close the console first.
5. Render the same camera in OpenBL with the `cam pos` and `cam ang` values, but **negate the pitch**: OpenBL's pitch is positive looking up, the game's is positive looking down. Use the window's size for `--size`. `--fov` uses the game's convention (horizontal at 4:3, `default_fov` 75), so the default needs no change.

Notes:

- `brightness` and gamma settings change the display's gamma ramp, which does not apply in windowed mode, so they do not affect captures. `mat_monitorgamma` does not exist in this engine.
- Things that make two shots differ even when the renderers agree: NPCs that moved, animated light styles and rotating props (ceiling fans) at a different moment, and the HUD and crosshair. Compare regions away from the HUD, and per-block statistics that tolerate small misregistration.

## Results

| What | Finding | How |
|---|---|---|
| Field of view | The game's 75 is horizontal at a 4:3 aspect: 59.86° vertical at any window shape | edge correlation of `sp_tutorial_1` captures against renders at 46.7°, 59.86° and 75° vertical (0.30 against about 0.05) |
| World lightmaps | Screen colour in gamma space = base × luxel^(1/2.2), i.e. **base × luxel** in linear terms (scale 1.0, not 2.3) | per-block fit of capture ÷ base texture against OpenBL's luxels, two captures of `sp_tutorial_1`: coefficient 0.97–0.99, exponent 0.43–0.45 in every channel |
| Props | Direct world lights plus 0.5 × the lightmaps gathered around the prop, at scale 1.0: the original is 1.04–1.08 / 0.99–1.06 / 1.08–1.15 as bright, about 18% spread between props. The old model (30% of the floor lightmap, scale 2.3) spread up to 84% and was too blue | same captures; per prop, the median of an eroded mask (pixels where renders with and without props differ), then the geometric mean over props |
| World, after the fix | Original ÷ OpenBL = 1.02–1.04 / 1.06–1.08 / 1.16 (linear means); the blue excess is in dark tones, from sRGB against pure gamma 2.2 ([RENDERING.md](RENDERING.md)) | same captures |
