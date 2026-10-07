# Save games (`.sav`)

Bloodlines writes saves to `save/` in the active game folder: `Vampire/save`, or the mod's own folder when a mod is active (`Unofficial_Patch/save`). Parser: `openbl/formats/sav.hpp`. The container layout below was worked out from three saves made with the GOG release and the Unofficial Patch. The `.HL1` state files are decoded; `.HL2` and `.HL3` are not yet.

**Privacy.** The original engine writes uninitialised memory into unused parts of fixed-size fields. In practice that includes fragments of the player's environment variables, such as their Windows user name and installed tool paths. Do not share save files publicly, and never commit them or excerpts from them.

## Container

| Part | Layout |
|---|---|
| Header | `"JSAV"`, `i32` version (117), `i32` header block size, `i32` token count, `i32` token table size |
| Token table | token-count zero-terminated strings. It is a hash table of 16,383 slots, so almost all are empty. The others hold field names (`GameHeader`, `mapCount`, `mapName`, `comment`, `userName`, `GLOBAL`). |
| Header block | records `{u16 size, u16 token}` followed by `size` bytes; the token is a slot in the token table |
| Embedded files | repeated to the end of the file: `char name[260]`, `i32` size, `i32` compressed size of the first chunk, then the data (below) |

**Header block.** `GameHeader` holds 4 (the number of fields that follow), then `mapCount`, `mapName` (32 bytes, the current map), `comment` (80 bytes: the map path and play time, as shown in the load menu), `userName` (80 bytes: the location name shown in the menu) and `GLOBAL` (0).

**Compression.** Embedded files are zlib-compressed in chunks of 512 KiB of uncompressed data. The first chunk's compressed size is in the entry header. Each later chunk is preceded by its own `i32` compressed size.

**Embedded files.** Three per visited map:

| File | Contents |
|---|---|
| `<map>.HL1` | entity state (up to about 1 MB): see below. Parser: `openbl/formats/save_state.hpp` |
| `<map>.HL2` | further state. Starts with `VALV`; not decoded yet. |
| `<map>.HL3` | a few bytes: a count and a short table; not decoded yet |

In the three test saves, every byte is accounted for and every chunk decompresses to its declared size.

## Map state files (`.HL1`)

These files describe themselves. Every value is a **record**, `{u16 size, u16 token, size bytes}`, where the token is a slot in the file's own name table. Records are grouped into **structs**: a record named after the type, holding an `i32` field count, followed by that many field records. Only fields that differ from their defaults are written. A field whose value is exactly a sequence of structs is a nested struct (for example `m_Collision` → `CCollisionProperty`).

| Part | Layout |
|---|---|
| Header | `"VALV"`, `i32` version 117, `i32` name table size, `i32` name count (16,383 slots), `i32` header section size, `i32` body section size |
| Name table | as in the container |
| Header section | `i32`, `i32`, `i32` block count, then per block `uv {elems {SaveRestoreBlockHeader_t {szName, locHeader, locBody}}}`. The blocks are **Entities**, **EventQueue**, **Physics**, **AI** and **Python**. |
| Entity table | at header section + Entities `locHeader`: `i32` count, then one `ETABLE` struct per entity slot (`id`, `edictindex`, `saveentityindex`, `location`, `size`, `flags_*`, `classname`). Slots without a size are free. |
| Body section | global structs (`Save Header`: skill, time, map and sky name; `ADJACENCY`: level transitions and landmarks; `LIGHTSTYLE`: the current pattern string of each light style), then the entity data |
| Entity data | at body + `location`, `size` bytes: one struct per class in the hierarchy, base first (`CBaseEntity`, `CBaseAnimating`, …, `CBasePlayer`, `CHL2_Player`). A few classes add raw bytes after the last struct: 1 for some NPC classes, 29 for the player. |
| Other blocks | at (start of the entity data) + `locBody`: `EventQueue` (`PEvent` structs), `Physics` (`PhysObjectHeader_t`), `AI` (a few raw bytes), `Python` |
| Python block | entries `{u8 1, i32 length, pickle}`. The pickles are Python protocol 0. The first is a dictionary of the scripts' global variables (`Story_State`, and with the Unofficial Patch flags such as `Patch_Plus`); the second is a smaller per-map dictionary. **Never unpickle untrusted data**: decode it with a restricted parser. |

The player entity holds the character sheet as array fields with readable element names: `m_iVAttributesBase[ v_attribute_strength ]`, `m_iVAbilitiesCurrent[ v_ability_stealth ]`, `m_iVDisciplinesBase[ v_discipline_auspex ]`, plus clan, generation, humanity, blood pool, health, position and hotkeys.

The light style strings saved here match the Quake patterns OpenBL uses for styles 1–12, which independently confirms them.

Totals over the three test saves: 9 map states, 5,224 entities, 245,206 saved fields, 18 pickles. 162 entities carry class-specific trailing bytes, kept raw.

`.HL2` files start with `VALV` too, but their header counts differ; `.HL3` holds a few integers. Both are still to decode.
