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
| `openbl-cli` inspection tool | Working |

## Building

Requirements: CMake 3.25+, Ninja, and a C++20 compiler (MSVC 19.40+, Clang 17+ or GCC 13+). Builds are 64-bit only. The first configure downloads pinned versions of zlib and doctest. To link the system zlib instead, pass `-DOPENBL_USE_SYSTEM_ZLIB=ON`.

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
```

## Contributing

Read [docs/LEGAL.md](docs/LEGAL.md) before contributing. Clean-room rules apply to everyone. After that, see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

The engine source code is licensed under [GPLv3](LICENSE). This license covers OpenBL's code only, never the game's data.
