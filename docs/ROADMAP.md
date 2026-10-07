# Roadmap

Each phase ends with something that can be demonstrated. Faithfulness is measured against the original game (see "Parity testing" below), not judged by eye.

## Phase 0: game data formats *(complete)*

Read every file type the game ships, and write the ones that tools will need. Each format gets a spec, unit tests and an integration test against the real install.

- [x] VPK archives (`packNNN.vpk`), including the directory-index variant
- [x] Virtual filesystem with loose-file and pack override order
- [x] Textures: `.tth`/`.ttz` pairs and embedded VTF 7.1, with DXT/RGB decoding to RGBA8
- [x] Loose `.tga` and `.png` images (reader; not yet wired into the virtual filesystem's texture loading)
- [x] Materials: `.vmt` and the KeyValues text syntax
- [x] Shaders: catalogue of every shader, parameter and proxy (`openbl-cli mat catalogue`, docs/formats/shaders.md, proxies.md)
- [x] Shaders: proxy captures from the original (9 stations); `LinearRamp` and `AnimatedTexture` timing confirmed ([PROXY_CAPTURE.md](PROXY_CAPTURE.md))
- [x] Models: `.mdl` v2531 geometry, bones, skins, textures; `.vtx` v107 triangle lists and LODs
- [x] Animations: descriptions, sequences, include models, run-length channel decoding, root-relative spine rotation, skinning matrices
- [x] Animations: blend grids and pose parameters, events, movement (root motion), fade times, Bloodlines melee hit data
- [x] Animations: facing and movement conventions (+x forward, move_yaw clockwise)
- [x] Animations: sequence layers, melee combo links (dodge, blocked reaction, next attack, follow-up), attack directions, combo window
- [x] Models: hitboxes, attachments, flex controllers and rules, mesh flexes (which vertices), mouths, IK chains, surface property
- [x] Models: packed normal tables for quantized vertices (closed form, recovered from the data)
- [x] Models: flex vertex deltas (packed directions with byte lengths; a float form on one model)
- [x] Models: cloth layout (particles, constraints, triangles, edges, per-vertex binding, collision capsules, per-LOD table)
- [x] Physics: `.phy` solids, convex pieces (bounding-tree walk), model-space conversion, text block
- [x] Physics: typed `.phy` text (solids, ragdoll constraints, joint merges, break pieces); ragdoll limits measured as rotations about the child bone's axes from the bind pose
- [x] Physics: collision hulls for map brushes (bevel sides skipped), cross-checked against the compiled world collision
- [x] Maps: `.bsp` v17 geometry, faces, displacements, lightmap data, static props, entities, pakfile
- [x] Maps: BSP nodes and leafs (line-of-sight traces), world lights
- [x] Maps: full leafs, brushes and sides, visibility (PVS/PAS), areas and area portals, cubemaps, detail props and their lighting, collision (IVPS and MOPP), vertex normals, primitives (crack-free triangulation), leaf water data
- [x] Maps: displacement lightmap sample positions (lump 34)
- [x] Maps: decals from `infodecal` entities on world and brush-entity faces (95.5% placed), rendered with lightmaps and DecalModulate
- [x] Coverage report: `openbl-cli fs coverage` lists every extension in the game data with its status
- [x] AI node graphs: `.ain` (text in Bloodlines) and `.loc`
- [x] Dialogue: `.dlg`. Lip-sync: `.lip`. Choreography: `.vcd`
- [x] `vdata` text tables parse (KeyValues with block comments, `\"` escapes, recovery from the shipped typos); catalogued in docs/formats/text_data.md
- [x] Typed loaders for the `vdata/system` rule tables: stats, feats, clan/NPC templates with inheritance, quests, vendors, discipline powers, leveling, histories, item tables
- [x] Typed loaders for the flat parameter tables: path lookup (rules), rule tables, trait effects, reactions, stealth grids, dice rolls, dispositions
- [x] Particle system definitions parse (`particles/*.txt`, 1,594 files)
- [x] Particle definitions: typed blocks and keyframe curves (`min~max`, `(t)`, `!`), sprites and child emitters resolved ([particles.md](formats/particles.md))
- [x] Facial expressions: `.vfe` and `expressions/*.txt`
- [x] Sound schemes (`sound/schemes/*.txt`)
- [x] Sound: `.wav` decoding (PCM and Microsoft ADPCM); `.sfk`/`.pk` identified as editor leftovers
- [x] Sound: `.mp3` decoding (minimp3, CC0); all 5,171 voice lines decode
- [x] Video: `.bik` container parser and decoding via FFmpeg libavcodec (LGPL, dynamic); all four intro movies decode
- [x] UI layouts and localisation: `.res`, `scripts/dialog_*`, `resource/gameui_english.txt` (UTF-16)
- [x] Fonts: `.fnt` bitmap fonts (glyph maps, pages, metrics)
- [x] `.bmp`: uncompressed bitmaps (the 25 shipped files are face-poser toolbar icons)
- [x] Key binding lists and `liblist.gam`
- [x] Special text formats: experience table, credits, sentences, titles, DSP presets
- [x] Console scripts: `.cfg`, `valve.rc` (commands), `settings.scr` (server-options layout)
- [x] Save games: `.sav` container and `.HL1` map state (self-describing records, entity table, globals, event queue, physics headers, Python pickles)
- [x] Save games: `.HL2` client state (client entities, decals), `.HL3` transition lists, restricted pickle reader, entity trailing data, name-table hash, writer with byte-exact record round trip
- [x] Save games: the original game loads a save written by OpenBL
- [x] Python: the engine embeds Python 2.1 (`.pyc` magic 60202); every compiled script has its source shipped (docs/formats/python.md)

## Phase 1: rendering and map viewer *(in progress)*

- [x] Window and input (SDL3)
- [x] Rendering hardware interface (RHI) with a Vulkan 1.3 backend
- [x] Viewer with free-fly camera and screenshot capture (draws a textured test cube)
- [ ] Direct3D 12 backend (shaders are already HLSL; add DXC for DXIL)
- [ ] Run the renderer clean under the Khronos validation layer
- [x] Map world geometry and brush entities, textured, with baked lightmaps (style 0)
- [x] Materials: patch materials, pakfile overrides, alpha test, translucency, self-illumination, two-layer blends
- [x] Static props and `prop_*` entities (LOD 0, default body group, skins)
- [x] Prop lighting: per-vertex ambient cube from visible style-0 world lights (BSP line-of-sight test)
- [x] Prop lighting: indirect light gathered from the surrounding lightmaps (162 traced rays), fitted to original-game captures
- [ ] Prop lighting: confirm in more rooms and outdoors (sky ambient); per-vertex lighting if the original uses it (`r_staticlighting`); lights with styles other than 0
- [x] Light styles: all lightmap styles drawn, weighted per style (animated Quake patterns at 10 letters/s; switchable styles 32+ on unless all their lights start dark); props include styled world lights
- [ ] Light styles: toggling switchable lights at runtime (needs entity I/O, Phase 2)
- [x] 2D skybox
- [x] Water placeholder: fog colour, Fresnel blend to a dim reflection tint, animated normal-map ripples
- [ ] Real water: reflection and refraction render targets, underwater fog, `$bottommaterial`, cheap-water distances
- [ ] Light styles, translucent sorting, cubemap reflections, prop fade distances
- [x] Parity tooling: viewer shot lists (`--shots`, `--fov`, `--size`), `openbl-cli image compare` (see docs/PARITY.md)
- [x] Lightmap brightness measured against original-game captures: base × luxel in linear terms (scale 1.0, was 2.3); capture method verified; `--fov` follows the game's 4:3-horizontal convention ([PARITY.md](PARITY.md))
- [ ] The ceiling fan in `sp_tutorial_1` is drawn without its ceiling rod (seen in the parity captures)
- [ ] Gamma-space shading option (the original multiplies in pure gamma 2.2; OpenBL's sRGB pipeline is up to ~15% darker in dark tones)
- [x] Static models in maps
- [x] Skinned and animated models: GPU skinning (per-draw bone data), NPCs from map entities playing their first `ACT_IDLE` sequence
- [x] Animation: blend grids across pose parameters, fades between sequences, viewer `--npc-sequence`/`--pose` ([ANIMATION.md](ANIMATION.md))
- [ ] Animation: sequence layers, root motion, facial flexes, eyes
- [ ] Free-fly viewer on Windows and Linux
- [x] Material proxies at runtime: Sine, LinearRamp, noise, arithmetic chains, AnimatedTexture (base and water normal frames), TextureScroll, TextureTransform, driving the base texture transform, `$color`/`$alpha` and `$selfillumtint` on the world and props; `--time` for fixed-clock renders
- [x] Material proxies: characters, `UnlitTwoTexture` second layer with `$texture2transform` (TV scanlines), `--sequence` frame captures
- [ ] Material proxies that need missing features: sprite fades (PlayerProximity, LampBeam, LampHalo; needs sprites), GlobalWetness (needs environment maps and rain), TextConsole (needs the terminal UI); confirm the ones the captures could not show (security-camera `Sine`, sprite fades, `GlobalWetness` in rain, ticker scroll speed)
- [x] Render-to-texture monitors (`func_monitor` + `point_camera`, aimed by `look_dest`): render targets in the RHI, one per visible monitor; checked against the hospital and haven TV captures
- [x] `StartHidden` entities are not drawn (brush entities, props, NPCs); `--show NAME` overrides it for a brush entity
- [ ] Monitors: confirm the camera `FOV` convention, picture brightness and render-target size; cameras parented to moving props
- [ ] Particle simulation and rendering, with the semantics confirmed against captures of the original
- [ ] Decals on static props (`$modelmaterial`, about 200 decals); confirm decal orientation against the original
- [ ] Cloth simulation for hair and clothing; confirm the cloth settings against the running game
- [ ] Animation: confirm the third fade time (+620) and the last two combo-window values in the running game

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
