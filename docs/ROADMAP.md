# Roadmap

Each phase ends with something that can be demonstrated. Faithfulness is measured against the original game (see "Parity testing" below), not judged by eye.

## Phase 0: game data formats *(in progress)*

Read every file type the game ships, and write the ones that tools will need. Each format gets a spec, unit tests and an integration test against the real install.

- [x] VPK archives (`packNNN.vpk`), including the directory-index variant
- [x] Virtual filesystem with loose-file and pack override order
- [x] Textures: `.tth`/`.ttz` pairs and embedded VTF 7.1, with DXT/RGB decoding to RGBA8
- [ ] Loose `.tga` images (UI, fonts)
- [ ] Materials: `.vmt`
- [ ] Models: `.mdl`, `.vtx`, `.vvd` (if present), `.phy`
- [ ] Maps: `.bsp` (Bloodlines variant), `.ain` (AI node graphs)
- [ ] Dialogue: `.dlg`. Lip-sync: `.lip`. Choreography: `.vcd`
- [ ] `vdata` text tables: items, hack terminals, strings, signs
- [ ] Fonts and UI: `.fnt`, `.res`, `.vfe`
- [ ] Save games (`.sav`)
- [ ] Python bytecode (`.pyc`) in archives; identify the embedded interpreter's version

## Phase 1: rendering and map viewer

- [ ] Rendering hardware interface (RHI) with Vulkan and Direct3D 12 backends
- [ ] Load any map: world geometry, lightmaps, static props
- [ ] Skinned and animated models
- [ ] Free-fly viewer on Windows and Linux

## Phase 2: simulation core

- [ ] Client/server entity system, with single-player running as a local server
- [ ] Data-driven registries for clans, disciplines, feats, items and NPC types (see ARCHITECTURE.md)
- [ ] Physics and collision
- [ ] Python scripting host that runs the game's original scripts unmodified
- [ ] Dialogue system, with lip-sync and choreography playback
- [ ] Input, audio, UI

## Phase 3: single-player parity

- [ ] Character creation through to every ending
- [ ] Load original save games
- [ ] Parity test suite passes for every map

## Phase 4: modding API

- [ ] Registries become stable, documented and versioned
- [ ] Mod loading: mounting, load order, conflict reporting
- [ ] Example mod that adds a new clan without overriding an existing one

## Phase 5: multiplayer

- [ ] Research the cut multiplayer content left in the game data
- [ ] Network replication on top of the Phase 2 client/server split
- [ ] Vampire and hunter factions with the cut hunter abilities, plus a human faction

## Phase 6: content tools

- [ ] Map, dialogue and asset pipelines for new content

## Parity testing

To prove "1:1", we need repeatable comparisons with the original game:

- **Format round-trips**: read and re-write every file in the install, and check the result byte for byte.
- **Visual**: compare captures of fixed camera positions in each map, original against OpenBL.
- **Logic**: trace script, dialogue and quest state from scripted playthroughs and diff the traces.
- **Saves**: load original saves, and check that saves written by OpenBL load in the original game where the formats allow it.
