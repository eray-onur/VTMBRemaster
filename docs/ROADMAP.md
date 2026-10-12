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
- [x] Model lighting from the original's shaders (`vertexlit_and_unlit_generic_vs20`): two local lights per model, evaluated per vertex (per pixel with a normal map) with Lambert, attenuation and spot cones; the rest in the ambient cube; the sun where the sky is seen, and sky ambient light in the gathered indirect light ([RENDERING.md](RENDERING.md#models-vertexlitgeneric))
- [ ] Prop lighting: confirm in more rooms and outdoors; static props' baked per-vertex lighting (the engine bakes it at load, `CModelRender::CreateStaticPropColorData`; its light set needs a capture near several strong lights); lights with styles other than 0
- [ ] Model lighting: compare near strong lights with the running game (`sm_hub_1`: the NPCs at the burning barrel, lamp posts, the 3D skybox's parking structure)
- [x] Light styles: all lightmap styles drawn, weighted per style (animated Quake patterns at 10 letters/s; switchable styles 32+ on unless all their lights start dark); props include styled world lights
- [ ] Light styles: toggling switchable lights at runtime (needs entity I/O, Phase 2)
- [x] 2D skybox
- [x] 3D skybox (`sky_camera` area drawn at 1/scale behind the world) and distance fog from `worldspawn`; skybox fog confirmed against the original
- [x] Fog strength measured on `ch_hub_1` and `sm_hub_1`: 1 − (1 − f)² of the linear fog (within 0.05 of the original)
- [x] Water, following the original's DX9 water shaders: reflection and refraction render targets per water plane (oblique clipping at the surface), dudv distortion projected from world units, (1 − cos)⁴ Fresnel, water fog by the view ray's length under the surface; warp amplitude checked against the `ch_lotus_1` pool capture ([RENDERING.md](RENDERING.md))
- [x] Water from below: the surface drawn with its `$bottommaterial` (`WaterWarp` is the water shader by another name), refraction of what is above and reflection of what is below, underwater fog around an eye in a water leaf; compared with `hw_warrens_4` captures ([RENDERING.md](RENDERING.md#water-water))
- [ ] Water: the surface from below is about 0.6 as bright as the original's; expensive water in the 3D skybox (reflections compared: OpenBL's are deliberately stronger than the original's at grazing angles)
- [x] Environment maps on the world: cube maps from the map pakfile (RHI cube textures), added after lighting with tint, contrast, saturation and the three mask kinds, as the original's DX9 shader does; road brightness checked on `sm_hub_1`; viewer `--no-envmap`
- [x] Environment maps on props and characters (`VertexLitGeneric`), nearest cubemap per model; checked on the `sm_hub_1` manhole
- [x] Environment maps: `$fresnelreflection` (unused; default none), `$envmapmode` (not read by the shaders), sphere maps resampled to cubes, cheap water (`$forcecheap`, distance blend; works in the 3D skybox)
- [x] Translucent sorting: one queue in the original's BSP order (world surfaces by node or nearest leaf, entities in the nearest leaf they touch, farthest first) across the world, props, characters and sprites ([RENDERING.md](RENDERING.md#translucent-order))
- [x] Static prop fades, read from `engine.dll`: linear in the squared distance from the main camera, used in every view; viewer `--no-prop-fade`
- [x] Particle effects in the translucent queue, one item per effect
- [ ] Static prop fades: compare with the running game
- [x] Sprites (`env_sprite`): render modes, colour and amount, orientation modes, depth-tested glows hidden only when the world blocks their centre (lamp-globe glows drawn, as the original does); viewer `--no-sprites`
- [x] Sprites: world glows (`rendermode` 9), the fast pulse (`renderfx` 2), and the `PlayerProximity`, `LampBeam` and `LampHalo` fades (formulas read from `client.dll`); glows keep their world size (confirmed in the client code)
- [x] Sprites checked against the original on `sm_hub_1` with `r_drawsprites` on/off captures: colour mode, additive, lamp-globe glows and the skyline light cones, within about 7% except a faint glow excess close to a lamp and cones at 0.87 ([RENDERING.md](RENDERING.md#sprites-env_sprite))
- [ ] Sprites: scripts switching sprites on and off (traffic lights; needs entity I/O, Phase 2); scale animations
- [x] Visibility culling: PVS and frustum per view for world batches (faces grouped by cluster), props, characters, sprites and particles; characters out of the main PVS not animated; 4–5× faster frames on `sm_hub_1` ([RENDERING.md](RENDERING.md#visibility)); viewer `--no-cull`
- [x] Visibility: area portals (`func_areaportal` with `StartOpen` 0, mostly behind doors, closed; windows open), translucent world faces and decals culled; viewer `--no-area-portals` ([RENDERING.md](RENDERING.md#visibility))
- [ ] Visibility: portals opening and closing with doors (Phase 2), `func_areaportalwindow` fade distances; a faint culled/unculled difference on one `sm_warehouse_1` view
- [x] Bump-mapped world surfaces: `$bumpmap` faces lit from their three directional lightmaps with the original's basis and weighted average ([RENDERING.md](RENDERING.md#bump-mapped-lightmaps)); viewer `--no-bump`
- [x] Bump-mapped envmap reflections (tangent frame from the texture axes, normal-map alpha mask) and bump-mapped `WorldVertexTransition` blends ([RENDERING.md](RENDERING.md#bump-mapped-lightmaps))
- [x] Normal maps on models: the ambient cube per pixel about the bumped normal, bumped envmap reflections; MDL tangents ([RENDERING.md](RENDERING.md#normal-maps-on-models))
- [ ] Bump maps: compare with the original (Warrens, temple, `sp_soc_3` tunnels, the temple's gold Buddha)
- [x] Detail textures (`$detail`, multiplied in at 2× on encoded colour; world, props, characters). No shipped map uses one: only shader tests and the cracked breakable glass ([RENDERING.md](RENDERING.md#world-surfaces-lightmappedgeneric))
- [x] Detail props: the `dprp` models baked into world-space buffers per material with their stored light, culled by cluster ([RENDERING.md](RENDERING.md#detail-props)); viewer `--no-detail-props`
- [ ] Detail props: compare with the running game (unlit grass modulated by its stored light; draw distance and fade)
- [x] Ropes and cables (`keyframe_rope`, `move_rope`): chains at rest drawn as camera-facing strips with the `Cable` shader's lighting and normal-map shading; sag fitted against an `sm_hub_1` capture ([RENDERING.md](RENDERING.md#ropes-and-cables)); viewer `--no-ropes`
- [ ] Ropes: compare a single-span rope with the running game (whole-chain slack or a fixed fraction); `Type`, ropes moving between keyframes
- [x] Character shadows, as observed in the original (`cl_v_shadows` on and off): one per character away from its brightest light when strong enough, a silhouette drawn into a render target and projected onto the world only, bluish shadow colour and falloff measured on `sm_hub_1` ([RENDERING.md](RENDERING.md#character-shadows)); viewer `--no-shadows`
- [ ] Character shadows: how the original picks the light and tilts the shadow (fitted on two scenes), shadows of moving characters
- [ ] Cheap water: compare with the original (the open sea around `la_dane_1`)
- [x] Parity tooling: viewer shot lists (`--shots`, `--fov`, `--size`), `openbl-cli image compare` (see docs/PARITY.md)
- [x] Lightmap brightness measured against original-game captures: base × luxel in linear terms (scale 1.0, was 2.3); capture method verified; `--fov` follows the game's 4:3-horizontal convention ([PARITY.md](PARITY.md))
- [x] Tool materials: faces hidden by their texinfo flags and by brush-entity class (triggers, dust motes, particle volumes) instead of by the `tools/` prefix; draws the `tools/black` ceiling-fan rod in `sp_tutorial_1` ([RENDERING.md](RENDERING.md#world-surfaces-lightmappedgeneric))
- [x] Gamma-space blending: plain 8-bit targets, shaders encode their output, so translucent surfaces, decals and particles blend on encoded values like the original
- [x] Pure gamma 2.2 shading: textures sampled as stored, decoded with x^2.2 and encoded with x^(1/2.2) in the shaders; the `sp_tutorial_1` world within 2% of the original's encoded brightness (was 0.71 in blue); prop indirect light refitted to 0.15 ([RENDERING.md](RENDERING.md#world-surfaces-lightmappedgeneric))
- [x] Static models in maps
- [x] Skinned and animated models: GPU skinning (per-draw bone data), NPCs from map entities playing their first `ACT_IDLE` sequence
- [x] Animation: blend grids across pose parameters, fades between sequences, viewer `--npc-sequence`/`--pose` ([ANIMATION.md](ANIMATION.md))
- [x] Eyes: irises drawn over the eyeball from the eye mesh's front projection, sized from the original
- [x] Facial flexes: controllers through the flex rules and mesh-flex ramps to GPU vertex offsets; viewer `--flex`
- [x] Animation: flex normal offsets (creases shade as faces move)
- [ ] Animation: sequence layers (all weapon overlays; need weapons and a look at combat in the original), root motion, flex drivers (expressions, lip-sync; Phase 2); eyes looking at a target, eye glints
- [ ] Free-fly viewer on Windows and Linux
- [x] Material proxies at runtime: Sine, LinearRamp, noise, arithmetic chains, AnimatedTexture (base and water normal frames), TextureScroll, TextureTransform, driving the base texture transform, `$color`/`$alpha` and `$selfillumtint` on the world and props; `--time` for fixed-clock renders
- [x] Material proxies: characters, `UnlitTwoTexture` second layer with `$texture2transform` (TV scanlines), `--sequence` frame captures
- [ ] Material proxies that need missing features: GlobalWetness (environment maps are done; needs rain), TextConsole (needs the terminal UI); confirm the ones the captures could not show (security-camera `Sine`, sprite fades, `GlobalWetness` in rain, ticker scroll speed)
- [x] Render-to-texture monitors (`func_monitor` + `point_camera`, aimed by `look_dest`): render targets in the RHI, one per visible monitor; checked against the hospital and haven TV captures
- [x] `StartHidden` entities are not drawn (brush entities, props, NPCs); `--show NAME` overrides it for a brush entity
- [ ] Monitors: confirm the camera `FOV` convention, picture brightness and render-target size; cameras parented to moving props
- [x] Particle simulation and rendering: active `env_particle` effects, emitters, curves, motion, premultiplied sprites ([PARTICLES.md](PARTICLES.md))
- [ ] Particles: calibrate units and colour scale on more captures (one done: barrel fire), collisions, refraction, `func_particle` rain, script-started effects, fire light
- [x] Decals: faces just behind the decal, displacements, and props (`$modelmaterial`); 4,520 of 4,609 on the world plus a few on props
- [ ] Decals: the remaining few dozen (graffiti 20 to 60 units off surfaces), `DecalModulate` on props; confirm orientation against the original
- [x] Cloth simulation for hair and clothing on characters: Verlet particles with the model's constraints (second group as minimum distances), capsules, shape pull; viewer `--no-cloth` ([ANIMATION.md](ANIMATION.md))
- [ ] Cloth: props (curtains, lanterns), the unknown piece parameter and settings records, comparison with the running game
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
