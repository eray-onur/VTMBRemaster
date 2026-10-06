# Roadmap

Each phase ends with something that can be demonstrated. Faithfulness is measured against the original game (see "Parity testing" below), not judged by eye.

## Phase 0: game data formats *(in progress)*

Read every file type the game ships, and write the ones that tools will need. Each format gets a spec, unit tests and an integration test against the real install.

- [x] VPK archives (`packNNN.vpk`), including the directory-index variant
- [x] Virtual filesystem with loose-file and pack override order
- [x] Textures: `.tth`/`.ttz` pairs and embedded VTF 7.1, with DXT/RGB decoding to RGBA8
- [ ] Loose `.tga` images (UI, fonts)
- [x] Materials: `.vmt` and the KeyValues text syntax
- [ ] Shaders: catalogue the parameters each of the 52 shaders uses; proxy behaviour
- [x] Models: `.mdl` v2531 geometry, bones, skins, textures; `.vtx` v107 triangle lists and LODs
- [ ] Models: packed normal tables for quantized vertices; animations and sequences; attachments, hitboxes, flexes
- [ ] Physics: `.phy`
- [x] Maps: `.bsp` v17 geometry, faces, displacements, lightmap data, static props, entities, pakfile
- [ ] Maps: BSP tree, leafs and visibility, brushes, world lights, cubemaps, detail props, physics collision
- [ ] AI node graphs: `.ain`
- [ ] Dialogue: `.dlg`. Lip-sync: `.lip`. Choreography: `.vcd`
- [ ] `vdata` text tables: items, hack terminals, strings, signs
- [ ] Fonts and UI: `.fnt`, `.res`, `.vfe`
- [ ] Save games (`.sav`)
- [ ] Python bytecode (`.pyc`) in archives; identify the embedded interpreter's version

## Phase 1: rendering and map viewer

- [x] Window and input (SDL3)
- [x] Rendering hardware interface (RHI) with a Vulkan 1.3 backend
- [x] Viewer with free-fly camera and screenshot capture (draws a textured test cube)
- [ ] Direct3D 12 backend (shaders are already HLSL; add DXC for DXIL)
- [ ] Run the renderer clean under the Khronos validation layer
- [x] Map world geometry and brush entities, textured, with baked lightmaps (style 0)
- [x] Materials: patch materials, pakfile overrides, alpha test, translucency, self-illumination, two-layer blends
- [x] Static props and `prop_*` entities (LOD 0, default body group, skins)
- [ ] Prop lighting from world lights and ambient cubes (currently the floor lightmap below each prop)
- [x] 2D skybox
- [x] Water placeholder: fog colour, Fresnel blend to a dim reflection tint, animated normal-map ripples
- [ ] Real water: reflection and refraction render targets, underwater fog, `$bottommaterial`, cheap-water distances
- [ ] Light styles, translucent sorting, cubemap reflections, prop fade distances
- [ ] Confirm the lightmap brightness (overbright scale 2.3) against original-game screenshots
- [x] Static models in maps
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
