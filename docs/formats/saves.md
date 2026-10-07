# Save games (`.sav`)

Bloodlines writes saves to `save/` in the active game folder: `Vampire/save`, or the mod's own folder when a mod is active (`Unofficial_Patch/save`). Parser: `openbl/formats/sav.hpp`. The container layout below was worked out from three saves made with the GOG release and the Unofficial Patch. All three embedded file types are decoded, and saves can be written back.

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
| `<map>.HL2` | client-side state: client entities and decals. See below. Parser: `parse_client_state` |
| `<map>.HL3` | transition list: `i32` count, then that many `.HL1` entity-table indices. Parser: `parse_transition_list` |

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
| Python block | entries `{u8 1, i32 length, pickle}`. The pickles are Python protocol 0. The first is a dictionary of the scripts' global variables (`Story_State`, and with the Unofficial Patch flags such as `Patch_Plus`); the second is a smaller per-map dictionary. **Never unpickle untrusted data.** `parse_pickle` (`openbl/formats/pickle.hpp`) is a restricted reader for protocols 0 and 1. It builds plain values and records class references and constructor calls as inert data without running them. All 18 pickles in the test saves decode to dictionaries (strings and integers only), and 9 of them hold `Story_State`. |

The player entity holds the character sheet as array fields with readable element names: `m_iVAttributesBase[ v_attribute_strength ]`, `m_iVAbilitiesCurrent[ v_ability_stealth ]`, `m_iVDisciplinesBase[ v_discipline_auspex ]`, plus clan, generation, humanity, blood pool, health, position and hotkeys.

The light style strings saved here match the Quake patterns OpenBL uses for styles 1–12, which independently confirms them.

Totals over the three test saves: 9 map states, 5,224 entities, 245,206 saved fields, 18 pickles. 162 entities carry class-specific trailing bytes, kept raw.

**Trailing class data.** The bytes after an entity's last struct are always a single zero byte for the NPC classes (`npc_maker`, `npc_VPedestrian`, `npc_VHumanCombatant`, `npc_VVampire`, `npc_VRat`). For the player they are always 29 bytes: `u8 1`, `i32 0`, `i32 0`, `i32 −1`, one `f32` that varies, then `f32[3]` of zeros. They're kept raw.

## Client state (`.HL2`)

Client state uses the same record format as `.HL1`, with a longer file header: `"VALV"`, then these `i32` fields:

| Field | Meaning |
|---|---|
| version | 117 |
| entity data size | size of the client entity data |
| header section size | size of the header section |
| decal section size | size of the decal list |
| name table size | bytes in the name table |
| decal count | number of decals in the list |
| name count | 16,383 |

After the name table come the header section, the client entity data and the decal list. The file ends with a length-prefixed string (`i32` length, then the text).

- **Header section:** a single `Entities` block and its entity table (`ETABLE` rows, as in `.HL1`).
- **Client entities:** `C_BaseEntity` structs.
- **Decal list:** `DECALLIST` structs (`position`, `flags`, `name` such as `rusta`, and `entityIndex`). These are the decals placed in the level.

Totals in the test saves: 9 client states, 1,541 client entity slots, 426 decals, and each decal count matches its header.

## Transition list (`.HL3`)

An `i32` count followed by that many indices into the map's `.HL1` entity table. These are the entities that travel with the player through a level change: always the player, plus carried items such as `item_w_unarmed`, `item_g_wallet`, `item_g_keyring` and `item_w_fists`. All 33 indices in the test saves are valid.

## Writing saves

`openbl/formats/save_write.hpp` provides the writer.

**Name tables.** Records refer to names by their slot in a 16,383-slot hash table, so a writer has to rebuild the table exactly. The hash, recovered from the saves, works on each byte in turn: rotate the 32-bit value right by 4 bits, then XOR in the byte. A name's slot is the hash modulo 16,383; if that slot is taken, it goes to the next free one. Across the 1,217 names in the test saves, 1,167 are at their hash slot and 50 are one or two slots after it.

**Functions:**

- `SaveTokenTable` rebuilds a name table.
- `encode_save_structs` writes records.
- `write_sav` writes a container, compressing in 512 KiB zlib chunks.

**Round-trip checks.** Rewriting the test saves gives:

- byte-identical uncompressed parts (signature, sizes, name table, header fields);
- identical embedded files after decompression;
- all 5,224 `.HL1` entities re-encoded byte for byte from their parsed records.

**The original game loads them.** A test save was rewritten with `openbl-cli save rewrite`, then loaded with `load` in the running GOG game with the Unofficial Patch. It opened in `sp_tutorial_1` with the character, NPCs and HUD restored. It was 16% smaller than the original only because of zlib's compression level. On loading any save, the game unpacks the embedded `.HL1`/`.HL2`/`.HL3` files into the save folder as working copies.
