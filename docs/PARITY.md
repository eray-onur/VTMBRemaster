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

A brightness ratio close to 1.0 across many shots is how the lightmap scale (currently 2.3) gets confirmed or corrected.

## Capturing reference shots in the original game

> **Unverified.** The commands below are the standard ones for Source engine games of this era. They have not yet been tried in Bloodlines, so please report what works.

1. Start the original game with the developer console enabled: add `-console` to its launch options in Steam.
2. Match OpenBL's settings: the same resolution as `--size`, and the same field of view as `--fov`. Check the game's field of view with the console command `fov` and pass that value to `--fov`.
3. In the console:
   ```
   map sm_pier_1
   sv_cheats 1
   setpos 3700 -700 -230
   setang -5 0 0
   cl_drawhud 0
   screenshot
   ```
   The screenshot is written as a TGA file, normally to a `screenshots` folder under `Vampire`. Rename it to the shot's name.
4. If `setpos` or `setang` are not available, use `noclip` with `cl_showpos 1` to read the position and angles from the screen, and write those into the shot list instead.

Things that make two shots differ even when the renderers agree: NPCs that moved, animated light styles at a different moment, and HUD or crosshair elements. Compare regions without NPCs and with steady light, or capture several shots and compare the medians.
