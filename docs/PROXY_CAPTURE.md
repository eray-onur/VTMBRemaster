# Capturing material proxies in the original game

Roadmap line 15: confirm how material proxies behave, such as scrolling, flicker, frame animation, wetness and distance fades, by capturing the original game at known positions and comparing with OpenBL at the same positions. Proxy behaviour lives in engine code, so the shipped files can't confirm it. See [formats/proxies.md](formats/proxies.md).

## What is known about the original's console

Checked by scanning the GOG install's DLLs for command names:

- **Available:** `noclip`, `god`, `notarget`, `sv_cheats`, `map`, `changelevel`, `screenshot`, `startmovie`/`endmovie`, `host_timescale`, `host_framerate`, `cl_showpos`, `cl_drawhud`, `r_drawviewmodel`, `mat_fullbright`, `r_drawentities`, `thirdperson`/`firstperson`, `bind`, `exec`, `cvarlist`, `find`.
- **Missing:** `setpos`, `setang`, `getpos` and `jpeg`. Stock Source `setpos` teleporting is not available.
- **Script API:** `vampire.dll` gives scripts `FindPlayer()` and entity methods `GetOrigin`, `SetOrigin`, `GetAngles` and `SetAngles`, documented as "Sets the origin of this entity" and so on. Shipped scripts call them with tuples, e.g. `SetOrigin((x, y, z))`. Teleporting therefore goes through Python, if the console accepts Python lines. That is step 0 below.

## Setup

**Unpatched game in a window.** GOG's "Legacy" entry runs `Vampire.exe` with no arguments, which is version 1.2 without the Unofficial Patch; the main entry adds `-game Unofficial_Patch`. The engine recognises `-window`, `-w`, `-h`, `-console`, `-dev` and `-condebug`:

```
"C:\GOG Galaxy\Games\VtMB\Vampire.exe" -window -w 1280 -h 720 -console
```

`-condebug` would also log the console to `console.log`, but that file and `startmovie`/`screenshot` output are written into the game folder. Move captures out after each session, and never write anything else there.

**Running minimized.** A Direct3D 9 game usually stops drawing while minimized, so a minimized window can't be captured. Run windowed instead, and the game can sit behind other windows. `start "" /min "C:\GOG Galaxy\Games\VtMB\Vampire.exe" -window ...` starts it minimized, but the game may restore itself, and it won't draw while minimized.

**Video recording.** The first footage file (`1_Prologue to Santa Monica Apartment.mp4`) has a fully black picture: peak luma is 16 on 579 of its 580 seconds, though the audio is intact. Screen capture can't see an exclusive-fullscreen Direct3D 9 game. In `-window` mode, OBS "Window Capture" or "Game Capture" works. For frame-exact comparisons, use the engine's own `startmovie` instead; see below.

**A throwaway save.** `map <name>` loads a map without its story state; NPCs and scripts may behave oddly. That's fine for looking at materials, but don't overwrite a real save.

## Step 0: confirm the tools (once)

Open the console with `~`, then:

```
sv_cheats 1
noclip
cl_showpos 1
cl_drawhud 0
r_drawviewmodel 0
```

1. **Python in the console.** Type `__main__.FindPlayer().GetOrigin()`.
   - If it prints a position, or at least no "Unknown command", teleporting works: `__main__.FindPlayer().SetOrigin((x, y, z))` and `__main__.FindPlayer().SetAngles((pitch, yaw, 0))`.
   - If not, fall back to flying with `noclip` and `cl_showpos 1` until the readout matches the station's coordinates. Every station is reachable that way, just slower.
2. **Eye height.** `SetOrigin` moves the player's feet; the camera is higher. Teleport to a station and compare `cl_showpos` with the coordinates you set. Use the difference (Source uses 64 units standing) for all stations. The tables below give **camera** positions.
3. **Angles.** Check that `SetAngles` turns the view and not just the body. If it only turns the body, aim by hand; `cl_showpos` shows the view angles.
4. **Deterministic time.** `host_framerate 0.0333` advances game time exactly 1/30 s per rendered frame, so `startmovie` frames are evenly spaced in game time. `host_timescale 0.25` slows everything down for watching by eye.

## Capture protocol (per station)

```
map <map>
sv_cheats 1; noclip; cl_drawhud 0; r_drawviewmodel 0
__main__.FindPlayer().SetOrigin((x, y, z - 64))
__main__.FindPlayer().SetAngles((pitch, yaw, 0))
host_framerate 0.0333
startmovie proxy_<station>
```

Wait about 3 seconds (90 frames), then:

```
endmovie
host_framerate 0
```

`startmovie` writes numbered frames into the game folder. Move them to your footage storage outside the repo, the same place as `docs/gameplay_footages/` (git-ignored, never committed).

The OpenBL side of each station:

```
openbl-viewer --map <map> --pos x y z --angles <-pitch> <yaw> --screenshot out.png
```

The viewer's pitch is the opposite sign of the console's: negative looks down. Yaw is the same.

## Stations

In order of when the game reaches them. Camera positions are 80 units in front of the surface.

| # | Station | Map | Camera (x y z) | Console pitch / yaw | Proxies covered | What to check |
|---|---|---|---|---|---|---|
| 1 | Hacking terminal | `sp_tutorial_1` | 183 128 148 | 0 / 0, then aim at the monitor | `TextConsole` | Terminal text drawing and refresh |
| 2 | Haven TV (`dev/dev_tvmonitor1a`) | `sm_apartment_1` | −43 45 0 | 0 / −45 | `Sine`, `LinearRamp`, `GaussianNoise`, `LessOrEqual`, `Subtract`, `Abs`, `Add`, `Exponential`, `TextureTransform` | Flicker rate and depth, picture roll speed and direction |
| 3 | News ticker (`signs/newsticker`) | `sm_apartment_1` | 1643 68 184 | 0 / 180 | `TextureScroll` | Scroll direction and speed in texture widths per second |
| 4 | Wet asphalt (`asphalt/asphaltasan`) | `sm_hub_1` | −1304 −888 −32 | about 60 / −180 (angled, so reflections show) | `GlobalWetness` | Reflection strength in rain and dry, and what drives it |
| 5 | Street light sprite | `sm_asylum_1` | −810 537 −201 | 0 / 0 | `PlayerProximity` (+ `Subtract`) | Fade against distance: capture at 80, 300 and 800 units |
| 6 | Security camera | `sm_basement_1` | 170 −663 −19 | 0 / 0 | `Sine` on a model | Blink period of the red light |
| 7 | Oil rig (`objects/oilriga`) | `sm_beachhouse_1` | −157 −293 4975 | 0 / −2 | `AnimatedTexture` | Frame rate and wrapping |
| 8 | Hollywood street light | `hw_hub_1` | −449 −1342 209 | 0 / 0 | `LampBeam`, `LampHalo` | Beam and halo fade against angle and distance |
| 9 | TV static (`objects/tvnoise`) | `la_hospital_1` | 672 −898 48 | 0 / 180 | `UniformNoise` | Noise per frame, or held for longer |
| 10 | Lotus water (`dev/dev_water2`) | `ch_lotus_1` | 866 0 −144 | about 60 / −180 | `WaterLOD` and the water shader | Cheap or expensive water; normal-map animation |

Stations 2 and 3 are in the haven, so a save there covers the most proxies.

**Not covered by stations:**

| Proxy | Why | How to check it |
|---|---|---|
| `BreakableSurface` | Glass that cracks when shot | Shoot any window and record the crack stages |
| `Shadow`, `ShadowModel` | Engine render-to-texture shadows, not material animation | Covered by general lighting parity instead |
| `Camo`, `PlayerPosition`, `PlayerSpeed`, `ParticleSphereProxy`, `Multiply` | Development or test materials only; none is placed in a shipped map | Not needed |

## What OpenBL needs for the comparison

- **Viewer time and frame sequences:** `--time T` to start the clock, plus a frame-sequence screenshot mode, so proxies can be captured at matching times and in matching sequences.
- **Proxy implementations** in the material system. Today proxies are catalogued but not run.
- **Image comparison:** `openbl-cli image compare` per frame, plus a timing comparison for animated values. Sine period and scroll speed are measured from frame sequences rather than single images.
