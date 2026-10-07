# Roadmap

Each phase ends with something that can be demonstrated. Faithfulness is measured against the original game (see "Parity testing" below), not judged by eye.

## Phase 0: game data formats *(in progress)*

Read every file type the game ships, and write the ones that tools will need. Each format gets a spec, unit tests and an integration test against the real install.

- [x] VPK archives (`packNNN.vpk`), including the directory-index variant
- [x] Virtual filesystem with loose-file and pack override order
- [x] Textures: `.tth`/`.ttz` pairs and embedded VTF 7.1, with DXT/RGB decoding to RGBA8
- [x] Loose `.tga` and `.png` images (reader; not yet wired into the virtual filesystem's texture loading)
- [x] Materials: `.vmt` and the KeyValues text syntax
- [x] Shaders: catalogue of every shader, parameter and proxy (`openbl-cli mat catalogue`, docs/formats/shaders.md, proxies.md)
- [ ] Shaders: verify proxy behaviour against the original game (capture plan: [PROXY_CAPTURE.md](PROXY_CAPTURE.md))
- [x] Models: `.mdl` v2531 geometry, bones, skins, textures; `.vtx` v107 triangle lists and LODs
- [x] Animations: descriptions, sequences, include models, run-length channel decoding, root-relative spine rotation, skinning matrices
- [x] Animations: blend grids and pose parameters, events, movement (root motion), fade times, Bloodlines melee hit data
- [x] Animations: facing and movement conventions (+x forward, move_yaw clockwise)
- [x] Animations: sequence layers, melee combo links (dodge, blocked reaction, next attack, follow-up), attack directions, combo window
- [ ] Animations: confirm the third fade time (+620) and the last two combo-window values in the running game
- [x] Models: hitboxes, attachments, flex controllers and rules, mesh flexes (which vertices), mouths, IK chains, surface property
- [x] Models: packed normal tables for quantized vertices (closed form, recovered from the data)
- [x] Models: flex vertex deltas (packed directions with byte lengths; a float form on one model)
- [x] Models: cloth layout (particles, constraints, triangles, edges, per-vertex binding, collision capsules, per-LOD table)
- [ ] Models: cloth semantics still to confirm against the running game (body parameter, byte array, vertex data, settings records)
- [x] Physics: `.phy` solids, convex pieces (bounding-tree walk), model-space conversion, text block
- [x] Physics: typed `.phy` text (solids, ragdoll constraints, joint merges, break pieces); ragdoll limits measured as rotations about the child bone's axes from the bind pose
- [x] Physics: collision hulls for map brushes (bevel sides skipped), cross-checked against the compiled world collision
- [x] Maps: `.bsp` v17 geometry, faces, displacements, lightmap data, static props, entities, pakfile
- [x] Maps: BSP nodes and leafs (line-of-sight traces), world lights
- [x] Maps: full leafs, brushes and sides, visibility (PVS/PAS), areas and area portals, cubemaps, detail props and their lighting, collision (IVPS and MOPP), vertex normals, primitives (crack-free triangulation), leaf water data
- [x] Maps: displacement lightmap sample positions (lump 34)
- [x] Maps: decals from `infodecal` entities on world and brush-entity faces (95.5% placed), rendered with lightmaps and DecalModulate
- [ ] Maps: decals on static props (`$modelmaterial`, about 200 decals); confirm decal orientation against the original
- [x] Coverage report: `openbl-cli fs coverage` lists every extension in the game data with its status
- [x] AI node graphs: `.ain` (text in Bloodlines) and `.loc`
- [x] Dialogue: `.dlg`. Lip-sync: `.lip`. Choreography: `.vcd`
- [x] `vdata` text tables parse (KeyValues with block comments, `\"` escapes, recovery from the shipped typos); catalogued in docs/formats/text_data.md
- [x] Typed loaders for the `vdata/system` rule tables: stats, feats, clan/NPC templates with inheritance, quests, vendors, discipline powers, leveling, histories, item tables
- [ ] Typed loaders for the flat parameter tables (rules, dice rolls, reactions, stealth, trait effects) when Phase 2 consumes them
- [x] Particle system definitions parse (`particles/*.txt`, 1,594 files)
- [ ] Particle system semantics and rendering
- [x] Facial expressions: `.vfe` and `expressions/*.txt`
- [x] Sound schemes (`sound/schemes/*.txt`)
- [x] Sound: `.wav` decoding (PCM and Microsoft ADPCM); `.sfk`/`.pk` identified as editor leftovers
- [x] Sound: `.mp3` decoding (minimp3, CC0); all 5,171 voice lines decode
- [ ] Video: `.bik` intro movies (codec choice to be decided)
- [x] UI layouts and localisation: `.res`, `scripts/dialog_*`, `resource/gameui_english.txt` (UTF-16)
- [x] Fonts: `.fnt` bitmap fonts (glyph maps, pages, metrics)
- [x] `.bmp`: uncompressed bitmaps (the 25 shipped files are face-poser toolbar icons)
- [x] Key binding lists and `liblist.gam`
- [x] Special text formats: experience table, credits, sentences, titles, DSP presets
- [x] Console scripts: `.cfg`, `valve.rc` (commands), `settings.scr` (server-options layout)
- [x] Save games: `.sav` container and `.HL1` map state (self-describing records, entity table, globals, event queue, physics headers, Python pickles)
- [ ] Save games: `.HL2`/`.HL3`, a restricted pickle reader, entity trailing data, writing saves
- [x] Python: the engine embeds Python 2.1 (`.pyc` magic 60202); every compiled script has its source shipped (docs/formats/python.md)

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
