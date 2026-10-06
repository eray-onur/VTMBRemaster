# Architecture

This document records decisions that are expensive to change later. Smaller decisions belong in the code.

## Layers

```
 game          rules, registries, Python script host, dialogue
 ─────────────────────────────────────────────────────────────
 server sim    authoritative entities, physics, AI     client: prediction, rendering, audio, UI
 ─────────────────────────────────────────────────────────────
 vfs           merged view of loose files, packs and mods
 formats       pure readers/writers, no engine dependencies
 platform      window, input, threads, file I/O
```

`formats` stays free of engine dependencies so that it can serve tools, tests and future editors.

## Decisions

### 1. Single-player is a local server

The game simulation runs as an authoritative server, and the client talks to it through the same replication interface in single-player as in multiplayer. Cut multiplayer is a Phase 5 feature, but adding this split later would mean rewriting every entity, so it exists from Phase 2 onward.

### 2. Content lives in open registries

The original game stores clans, disciplines, feats and item types in fixed tables. OpenBL replaces each table with a registry:

- Entries are keyed by string IDs (for example `clan.brujah`), not by fixed numeric indices.
- The original game's content is the base data set, loaded the same way a mod would be loaded.
- Mods add new entries, or patch specific fields of existing entries, without replacing whole files.
- Where the original data, scripts or save files use numeric IDs, a compatibility map translates them, so the original Python scripts and saves keep working unchanged.

### 3. The original scripts run unmodified

Bloodlines' game logic is largely written in Python and ships in the game data. OpenBL hosts these scripts and reimplements the module API that the original engine exposes to them. It does not rewrite their logic. The host's API surface is documented under `docs/scripting/` as it is reverse-engineered by observing the game.

### 4. Rendering goes through an RHI

The renderer talks to a thin rendering hardware interface (RHI) with two backends: Vulkan (Windows and Linux) and Direct3D 12 (Windows). The original look is reproduced first. Modern lighting is added as optional render paths, never by changing the defaults.

**Current shape** (`src/rhi/include/openbl/rhi/rhi.hpp`):

- **Objects:** buffers, textures, samplers, pipelines, bind groups and one command list per frame.
- **Resource layout:** every pipeline uses the same one. Set 0 holds a sampler at binding 0 and N textures at bindings 1..N. That maps to a Vulkan descriptor set or a D3D12 descriptor table.
- **Per-draw data:** small values go in push constants (root constants in D3D12). Larger blocks, such as bone matrices, go through `CommandList::set_shader_data`. It copies them into a per-frame upload buffer and binds them as a read-only structured buffer at set/space 1, binding 0, using a dynamic offset (a root SRV in D3D12).
- **Shaders are written in HLSL** (`shaders/`). glslang compiles them to SPIR-V at build time; DXC will produce DXIL for D3D12 from the same source.
- **Vulkan 1.3 backend:** dynamic rendering, synchronization2, two frames in flight, and memory through VMA. Resources are destroyed only once the GPU has finished every frame that might use them.
- **Captures:** `request_capture()` and `take_capture()` read back the frame, which the parity tests will build on.

### 5. Game paths are canonical

All game paths are normalized: lowercase, `/` separators (`formats::normalize_game_path`). This matches the shipped archives, and makes lookups behave the same on case-sensitive filesystems.

### 6. File priority

From highest priority to lowest: mods (Phase 4), then loose files in `Vampire/`, then `packNNN.vpk` archives with higher numbers first. See [formats/vpk.md](formats/vpk.md) for the evidence.
