# VPK archives (`packNNN.vpk`)

Troika's own archive format. It is **unrelated** to the later Valve VPK format (`*_dir.vpk`) despite sharing the extension.

Implementation: `src/formats/vpk.cpp`.

## Layout

All integers are little-endian `u32` unless noted otherwise.

```
[file data ...]                 concatenated, uncompressed, no padding or alignment
[directory]                     entry_count entries, starting at directory_offset
[footer, 9 bytes]
    u32 entry_count
    u32 directory_offset
    u8  kind                    0 = data archive, 1 = directory index
```

Each directory entry:

```
u32  name_length
char name[name_length]          not NUL-terminated
u32  data_offset                absolute offset in this file
u32  data_size
```

The directory runs from `directory_offset` to exactly the start of the footer. An empty archive is just the 9-byte footer.

## Directory-index archives (kind = 1)

`pack010.vpk` in the Steam release has `kind = 1` and `directory_offset = 0`. It holds 3,568 entries, all with offset 0 and size 0. Each name is a **directory** path (`cfg`, `dlg/chinatown`, `vdata/items`, …), and none of the names is a file in any other archive. The game presumably uses it to enumerate directories without scanning every archive. OpenBL treats it as a source of directory listings only.

## Observations from the Steam release

These were verified by parsing every archive of the Steam version.

| Archive | Entries | Notes |
|---|---|---|
| pack000–pack009 | 1,646 – 14,105 each | ~210 MB each, except pack009 (~150 MB) |
| pack010 | 3,568 | directory index |
| pack100, pack102 | 0 | 9-byte empty archives |
| pack101 | 6,277 | |
| pack103 | 7 | patch: dialogue and `vdata` files that also exist in pack101 |

- All names are lowercase ASCII with `/` separators. There are no leading slashes and no NUL terminators.
- Data is stored uncompressed (for example, `.wav` entries start with `RIFF`).
- The 67,476 file entries resolve to 67,469 unique paths. All 7 duplicates are pack101 files that pack103 also provides.
- 5,312 loose files exist under `Vampire/`. In the stock install, none of them collides with a packed path.

## Override order

These rules are **inferred, not yet confirmed against the original engine**:

- **Higher pack number wins.** pack103 holds only patched copies of pack101 files, which points to a patch mechanism.
- **Loose files beat archives.** Community patches ship as loose files and work.

To do: confirm both rules by placing a modified copy of a file at a lower priority and checking which version the original game loads.

## Writer

`write_vpk` produces exactly the layout above. It writes the data blobs in directory order. For pack010, pack100 and pack103, rewriting their entries in the same order reproduces the original file byte for byte. pack101 does not round-trip this way, because its data blobs are stored in a different order from its directory entries. Readers must not assume that data follows directory order.
