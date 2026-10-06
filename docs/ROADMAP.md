# Roadmap

Each phase ends with something that can be demonstrated. Faithfulness is measured against the original game (see "Parity testing" below), not judged by eye.

## Phase 0: game data formats *(in progress)*

Read every file type the game ships, and write the ones that tools will need. Each format gets a spec, unit tests and an integration test against the real install.

- [x] VPK archives (`packNNN.vpk`), including the directory-index variant
- [x] Virtual filesystem with loose-file and pack override order
- [x] Textures: `.tth`/`.ttz` pairs and embedded VTF 7.1, with DXT/RGB decoding to RGBA8
- [x] Loose `.tga` and `.png` images (reader; not yet wired into the virtual filesystem's texture loading)
- [x] Materials: `.vmt` and the KeyValues text syntax
- [ ] Shaders: catalogue the parameters each of the 52 shaders uses; proxy behaviour
- [x] Models: `.mdl` v2531 geometry, bones, skins, textures; `.vtx` v107 triangle lists and LODs
- [x] Animations: descriptions, sequences, include models, run-length channel decoding, root-relative spine rotation, skinning matrices
- [ ] Animations: multi-blend sequences, movement, events, IK; root facing convention
- [ ] Models: packed normal tables for quantized vertices; attachments, hitboxes, flexes
- [ ] Physics: `.phy`
- [x] Maps: `.bsp` v17 geometry, faces, displacements, lightmap data, static props, entities, pakfile
- [x] Maps: BSP nodes and leafs (line-of-sight traces), world lights
- [ ] Maps: visibility (PVS), brushes and brush sides, cubemaps, detail props, physics collision, overlays/decals
- [x] Coverage report: `openbl-cli fs coverage` lists every extension in the game data with its status
- [x] AI node graphs: `.ain` (text in Bloodlines) and `.loc`
- [x] Dialogue: `.dlg`. Lip-sync: `.lip`. Choreography: `.vcd`
- [x] `vdata` text tables parse (KeyValues with block comments, `\"` escapes, recovery from the shipped typos); catalogued in docs/formats/text_data.md
- [ ] Typed loaders for the `vdata/system` rule tables (with the Phase 2 registries)
- [x] Particle system definitions parse (`particles/*.txt`, 1,594 files)
- [ ] Particle system semantics and rendering
- [x] Facial expressions: `.vfe` and `expressions/*.txt`
- [x] Sound schemes (`sound/schemes/*.txt`)
- [ ] Sound: `.sfk` waveform previews, `.pk`; decoding `.wav` and `.mp3`
- [ ] Video: `.bik` intro movies (codec choice to be decided)
- [x] UI layouts and localisation: `.res`, `scripts/dialog_*`, `resource/gameui_english.txt` (UTF-16)
- [ ] Fonts and UI images: `.fnt`, `.bmp`
- [x] Key binding lists and `liblist.gam`
- [ ] Special text formats: `scripts/titles.txt`, `sentences.txt`, `dsp_presets.txt`, `credits.txt`, `experience_table.txt`
- [ ] Console scripts: `.cfg`, `valve.rc`, `settings.scr` (with the console in Phase 2)
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
- [x] Prop lighting: per-vertex ambient cube from visible style-0 world lights (BSP line-of-sight test)
- [ ] Prop lighting: replace the bounce estimate (30% of the floor lightmap) with the original's ambient term; lights with styles other than 0
- [x] Light styles: all lightmap styles drawn, weighted per style (animated Quake patterns at 10 letters/s; switchable styles 32+ on unless all their lights start dark); props include styled world lights
- [ ] Light styles: toggling switchable lights at runtime (needs entity I/O, Phase 2)
- [x] 2D skybox
- [x] Water placeholder: fog colour, Fresnel blend to a dim reflection tint, animated normal-map ripples
- [ ] Real water: reflection and refraction render targets, underwater fog, `$bottommaterial`, cheap-water distances
- [ ] Light styles, translucent sorting, cubemap reflections, prop fade distances
- [x] Parity tooling: viewer shot lists (`--shots`, `--fov`, `--size`), `openbl-cli image compare` (see docs/PARITY.md)
- [ ] Confirm the lightmap brightness (overbright scale 2.3) against original-game screenshots; verify the capture commands
- [x] Static models in maps
- [x] Skinned and animated models: GPU skinning (per-draw bone data), NPCs from map entities playing their first `ACT_IDLE` sequence
- [ ] Animation: blending between sequences, multi-blend sequences, facial flexes, eyes
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
