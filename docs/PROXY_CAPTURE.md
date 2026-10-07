# Capturing material proxies in the original game

Roadmap line 15: confirm how material proxies behave, such as scrolling, flicker, frame animation, wetness and distance fades, by capturing the original game at known positions and comparing with OpenBL at the same positions. Proxy behaviour lives in engine code, so the shipped files can't confirm it. See [formats/proxies.md](formats/proxies.md).

## What is known about the original's console

Checked by scanning the GOG install's DLLs for command names:

- **Available:** `noclip`, `god`, `notarget`, `sv_cheats`, `map`, `changelevel`, `screenshot`, `startmovie`/`endmovie`, `host_timescale`, `host_framerate`, `cl_showpos`, `cl_drawhud`, `r_drawviewmodel`, `mat_fullbright`, `r_drawentities`, `thirdperson`/`firstperson`, `bind`, `exec`, `cvarlist`, `find`.
- **Missing:** `setpos`, `setang`, `getpos` and `jpeg`. Stock Source `setpos` teleporting is not available.
- **Script API:** `vampire.dll` gives scripts `FindPlayer()` and entity methods `GetOrigin`, `SetOrigin`, `GetAngles` and `SetAngles`, documented as "Sets the origin of this entity" and so on. Shipped scripts call them with tuples, e.g. `SetOrigin((x, y, z))`. Teleporting therefore goes through Python typed into the console, which works (see below).

## Setup

**Unpatched game in a window.** GOG's "Legacy" entry runs `Vampire.exe` with no arguments, which is version 1.2 without the Unofficial Patch; the main entry adds `-game Unofficial_Patch`. The engine recognises `-window`, `-w`, `-h`, `-console`, `-dev` and `-condebug`:

```
"C:\GOG Galaxy\Games\VtMB\Vampire.exe" -window -w 1280 -h 720 -console
```

This works: footage recorded this way shows a titled 1280×720 window (1288×756 including the frame), with a normal picture throughout.

`-condebug` would also log the console to `console.log`, but that file and `startmovie`/`screenshot` output are written into the game folder. Move captures out after each session, and never write anything else there.

**Running minimized.** A Direct3D 9 game usually stops drawing while minimized, so a minimized window can't be captured. Run windowed instead, and the game can sit behind other windows. `start "" /min "C:\GOG Galaxy\Games\VtMB\Vampire.exe" -window ...` starts it minimized, but the game may restore itself, and it won't draw while minimized.

**Footage so far.** `1_Prologue part 1.mp4` (7 minutes, windowed) runs from the menu through character creation, the embrace, the theatre trial and Jack's alleys up to the garage. It shows no proxy material: the embrace map has none, the theatre's only proxy (`GlobalWetness`) is on the street outside, and the tutorial terminal (`TextConsole`) comes after the garage.

**Video recording.** The first footage file (`1_Prologue to Santa Monica Apartment.mp4`) has a fully black picture: peak luma is 16 on 579 of its 580 seconds, though the audio is intact. Screen capture can't see an exclusive-fullscreen Direct3D 9 game. In `-window` mode, OBS "Window Capture" or "Game Capture" works. For frame-exact comparisons, use the engine's own `startmovie` instead; see below.

**A throwaway save.** `map <name>` loads a map without its story state; NPCs and scripts may behave oddly. That's fine for looking at materials, but don't overwrite a real save.

## What the first capture run established

From `proxies.mp4`, a recording of a full run through the stations:

- **Teleporting works.** `__main__.FindPlayer().SetOrigin((x, y, z))` puts the player's feet exactly at the given point (`cl_showpos` reads back the same numbers).
- **Eye height is 64.** In first person the camera sits exactly 64 units above the feet. In third person it sits about 76 above and 50 behind, so press the first-person key before recording.
- **`SetAngles` does not turn the view.** Turn with the mouse until the second `cam ang` number in the `cl_showpos` readout matches the station's yaw. The first number is pitch: positive looks down, and it may display wrapped, e.g. 368 for 8.
- **The view updates only after the console closes.** Close it before judging the view.
- **Use OBS "Window Capture" on the game window, not a desktop capture.** In a desktop capture the game is hidden whenever another window covers it.

## Capture protocol (per station)

Open the console with `~` and enter:

```
map <map>
sv_cheats 1
noclip
cl_showpos 1
__main__.FindPlayer().SetOrigin((x, y, z))
```

Close the console and switch to first person. Turn until `cam ang` shows the station's pitch and yaw. Then stay still and record 15 seconds of the window. Name the clip after the station number.

The OpenBL side of each station:

```
openbl-viewer --map <map> --pos x y z+64 --angles <-pitch> <yaw> --screenshot out.png
```

The viewer's pitch is the opposite sign of the console's: negative looks down. Yaw is the same.

## Stations (captured)

Captured automatically by driving the running game window: console commands typed as keystrokes, injected mouse turns, and screen capture of the game area at 30 fps. The clips are in `docs/gameplay_footages/proxy_captures/` (git-ignored). Positions and angles below are read from the game's `cl_showpos` readout during each clip; the camera is the feet position plus 64 in z.

| # | Clip | Map | Feet (x y z) | View pitch / yaw | Proxies | Notes |
|---|---|---|---|---|---|---|
| 1 | `st01_tutorial_terminal` | `sp_tutorial_1` | 263 200 90 | −7.66 / 268.32 | `TextConsole` | Black screen with "Brothers Downtown Garage" in the lower right |
| 2 | `st02_haven_tv_ticker` | `sm_apartment_1` | −160 388 36 | 12.91 / 309.08 | TV chain + `TextureScroll` | TV switched on with `ent_fire tv_screen ScriptUnhide`; the newscaster feed and the scrolling ticker show |
| 4 | `st04_hub_asphalt` | `sm_hub_1` | −1304 −888 −111 | 40.79 / 0 | `GlobalWetness` | Standing on the alley floor, no rain; manhole in view |
| 5 | `st05_asylum_light_backing` | `sm_asylum_1` | from −810 537 −265, backing away along −x | 0 / 0 | `PlayerProximity` | No sprite glow visible up close or at about 240 units |
| 6 | `st06_security_camera_side`, `_front` | `sm_basement_1` | 170 −663 −89 and 250 −743 −89 | 0 / 359.40 and 0 / 85.13 | `Sine` | No red light lit; it may need the camera to be triggered |
| 7 | `st07_oil_rig` | `sm_beachhouse_1` | −477 −282 4911 | 0 / 356.08 | `AnimatedTexture` | Rig small, near the centre |
| 8 | `st08_hollywood_light_still`, `_backing` | `hw_hub_1` | −449 −1342 145, then backing about 300 units | 0 / 356.66 | `LampBeam`, `LampHalo` | Lamp head centred |
| 9 | `st09_hospital_monitors` | `la_hospital_1` | 520 −898 −16 | 0 / 351.52 | `UniformNoise` | Two monitors; the left shows a camera feed, the right is dark |
| 10 | `st10_lotus_water` | `ch_lotus_1` | 866 0 −208 | 66.95 / 1.33 | `WaterLOD` + water shader | Looking down into the pool |

**Setup that made the captures repeatable:**

- `sensitivity 1`, `m_yaw 0.022` and `m_pitch 0.022` give about 0.068° of yaw and 0.061° of pitch per injected mouse count. That isn't quite linear, so each aim is checked against the readout.
- `noclip` is a toggle, so don't send it twice.
- A teleport typed while the map is still loading is ignored.
- Facing directions can't be derived blindly. The hospital monitor screens face away from their brush normals, and the haven TV faces opposite its prop yaw; the tutorial terminal faces along its prop yaw. Check each view with a frame grab before recording.

There is no station 3 any more. The haven TV is a `func_monitor` (`tv_screen`) showing a live feed from a `point_camera` (`news_camera`) that films the news-ticker strip in a hidden room. The TV starts hidden until switched on, so watching it covers the ticker too. It also means OpenBL needs render-to-texture monitors before this station can match.

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
