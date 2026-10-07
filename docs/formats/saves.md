# Save games (`.sav`)

Bloodlines writes saves to `save/` in the active game folder: `Vampire/save`, or the mod's own folder when a mod is active (`Unofficial_Patch/save`). Parser: `openbl/formats/sav.hpp`. The container layout below was worked out from three saves made with the GOG release and the Unofficial Patch. The per-map state files inside are not decoded yet.

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
| `<map>.HL1` | entity state (up to about 1 MB). Starts with `VALV`; not decoded yet. |
| `<map>.HL2` | further state. Starts with `VALV`; not decoded yet. |
| `<map>.HL3` | a few bytes: a count and a short table; not decoded yet |

In the three test saves, every byte is accounted for and every chunk decompresses to its declared size.
