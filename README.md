# OpenBL

OpenBL is an open-source, clean-room engine reimplementation that runs *Vampire: The Masquerade – Bloodlines* (2004) using the game data from your own retail, Steam or GOG copy.

The goals:

- **Faithful.** Reproduce the original game's behaviour 1:1, verified against the original executable.
- **Modern.** 64-bit, Vulkan and Direct3D 12 renderers, modern lighting.
- **Moddable without limits.** Clans, items, disciplines and other content live in open registries. Mods add new entries instead of overwriting existing ones.
- **Complete.** Restore Troika's cut multiplayer mode, including hunter abilities and a human faction.

OpenBL is an unofficial fan project. It is not affiliated with or endorsed by Paradox Interactive, Activision or Valve. It contains no game assets: you need a legitimate copy of the game.

## Status

Early Phase 0 (game data formats). See [docs/ROADMAP.md](docs/ROADMAP.md).

What works today:

| Component | State |
|---|---|
| VPK archives (read and write) | Verified against all 15 archives of the Steam release |
| Virtual filesystem (loose files over packs, override order) | Working |
| Textures (`.tth`/`.ttz`, VTF 7.1, DXT decoding) | Verified against all 10,909 textures of the Steam release |
| Materials (`.vmt`, KeyValues text) | Verified against all 11,052 materials of the Steam release |
| Model geometry (`.mdl` v2531, `.vtx` v107) | Verified against all 4,255 models of the Steam release; animations not yet |
| Maps (`.bsp` v17: faces, displacements, static props, entities, pakfile) | Verified against all 101 maps of the Steam release |
| `openbl-cli` inspection tool | Working |
| Renderer: window, input, Vulkan 1.3 backend, `openbl-viewer` | Draws maps with textures, lightmaps, props, animated NPCs, the sky and placeholder water |

## Building

Requirements: CMake 3.25+, Ninja, and a C++20 compiler (MSVC 19.40+, Clang 17+ or GCC 13+). Builds are 64-bit only. To run the viewer you need a GPU and driver with Vulkan 1.3; the Vulkan SDK is **not** required.

The first configure downloads pinned versions of the dependencies: zlib, minimp3 (public domain, MP3 voice lines), FFmpeg (LGPL, Bink movies; a prebuilt shared build on Windows, the system FFmpeg elsewhere) and doctest, plus SDL3, the Vulkan headers, volk, Vulkan Memory Allocator, GLM and glslang for the renderer. glslang is used only at build time, to compile the HLSL shaders in `shaders/` to SPIR-V. Useful options:

- `-DOPENBL_BUILD_VIEWER=OFF` builds only the format libraries and the CLI, without any renderer dependencies.
- `-DOPENBL_USE_SYSTEM_ZLIB=ON` links the system zlib instead of building it.
- `-DOPENBL_WITH_FFMPEG=OFF` builds without FFmpeg; the Bink intro movies then can't be played.

On Windows, open a *x64 Native Tools* prompt for Visual Studio, then run:

```sh
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

On Linux, use the `linux-debug` preset instead.

### Tests against real game data

Integration tests and the CLI read the game location from `OPENBL_GAME_DIR`. Point it at either the install root or its `Vampire` folder:

```powershell
$env:OPENBL_GAME_DIR = "C:\Steam\steamapps\common\Vampire The Masquerade - Bloodlines"
ctest --preset windows-debug
```

When the variable is unset, the integration tests are skipped.

## Using openbl-cli

```sh
openbl-cli fs stat                        # list mounted archives and file counts
openbl-cli fs which dlg/hollywood/isaac.dlg  # show which archive or loose file provides a path
openbl-cli fs cat python/vamputil.py      # print a file from the merged view
openbl-cli vpk extract pack103.vpk out/   # extract one archive
openbl-cli tex info materials/brick/bradwllb            # texture format, size, mips, damage
openbl-cli tex export materials/brick/bradwllb out.tga  # decode the largest mip to TGA
openbl-cli mat info brick/bradwllb                      # shader, parameters, proxies, warnings
openbl-cli mdl info character/npc/common/cop/cop        # bones, textures, meshes, triangles
openbl-cli mdl export character/npc/common/cop/cop cop.obj  # geometry to OBJ (LOD 0)
openbl-cli bsp info sm_pier_1                           # faces, props, materials, entity classes
openbl-cli bsp export sm_pier_1 pier.obj                # world geometry to OBJ
```

## Using openbl-viewer

```sh
openbl-viewer --map ch_lotus_1                        # fly through a map, starting at the player start
openbl-viewer --map sm_pier_1 --pos 3700 -700 -230 --angles -5 0 --screenshot pier.png
openbl-viewer [--game DIR] [--map NAME] [--show TARGETNAME]... [--npc-sequence NAME] [--pose NAME=VALUE]... [--flex CONTROLLER=VALUE]... [--pos X Y Z] [--angles PITCH YAW] [--light-scale F]
              [--fov DEG] [--size WxH] [--fullbright] [--no-fog] [--no-light-styles] [--no-props] [--no-characters]
              [--screenshot out.png [--frames N]] [--time T] [--sequence N [--fps F]] [--shots FILE --out DIR] [--validation] [--no-vsync]
```

Without `--map`, the viewer shows a textured test cube.

Controls: hold the right mouse button to look around, **WASD** to move, **E**/**Q** (or **Space**/**Ctrl**) to go up and down, **Shift** to move faster, the mouse wheel to change speed, **C** to copy the current view as `--pos X Y Z --angles PITCH YAW` (also printed, with a ready `--shots` line), **P** to pause the scene clock (animations, light styles, particles), **Esc** to quit. The window title shows the position, angles, frame rate and speed.

`--screenshot` renders N frames, saves the back buffer as a PNG, and exits. `--time` fixes the scene clock at T seconds, so animated materials and light styles render the same way every time; `--sequence N` then writes N frames, `1/F` seconds apart (default 30 fps), as `out_000.png`, `out_001.png`, …. `--fov` uses the game's convention (horizontal at a 4:3 aspect; default 75). `--shots` renders a list of camera positions to PNGs; together with `openbl-cli image compare` it is used to compare OpenBL against the original game (see [docs/PARITY.md](docs/PARITY.md)). `--show` draws a `StartHidden` brush entity anyway (like `ent_fire NAME ScriptUnhide` in the game). `--validation` enables the Khronos validation layer, if it's installed (it ships with the Vulkan SDK).

## Contributing

Read [docs/LEGAL.md](docs/LEGAL.md) before contributing. Clean-room rules apply to everyone. After that, see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

The engine source code is licensed under [GPLv3](LICENSE). This license covers OpenBL's code only, never the game's data.
