# Bitmap fonts (`.fnt`)

UI fonts are pre-rendered bitmap fonts in `materials/fonts/`, one file per face, size and weight (`tahoma_07_000_000.fnt`, `times_new_roman_86_900_000.fnt`; 141 in the Steam release). Glyph images live in **page textures** named `<font file name>-page<N>` (ordinary `.vmt` + `.tth`/`.ttz` in the same folder). `fontlist.txt` lists the fonts. Parser: `openbl/formats/fnt.hpp`.

## Layout

| Offset | Contents |
|---|---|
| 0 | `i32` page count (1–9) |
| 4 | `i32` offset of the footer |
| 8 | `i32` 1 |
| 12 | five `i32` size values; the second is the point size, the others look like ascent and line heights (not pinned down) |
| 32 | `i32` offset of the glyph table, always 292 |
| 36 | 256 bytes: glyph index for each character code (unmapped characters point at a default glyph) |
| 292 | 44-byte glyphs, up to the footer |
| footer | one `{i32 width, i32 height, i32, i32 0}` per page, then page names |

**Glyph** (44 bytes):

| Offset | Field |
|---|---|
| 0 | 18 bytes of small per-glyph values; not decoded (perhaps per-row extents) |
| 18 | `u16`, 0–255; not decoded |
| 20 | `i16` vertical offset of the glyph's top from the baseline (negative = above) |
| 22 | `i16` horizontal offset, probably the left bearing |
| 24 | `i16` page index |
| 26 | `i16` pixel width: matches the texture rectangle for all 26,524 glyphs |
| 28 | `f32` u0, v0, u1, v1 on the page texture |

**Page names.** The names stored after the page sizes are unreliable: a two-page font stores `…-page1` followed by a garbled `…-page2`. The page textures that exist always follow the `<font>-page<N>` pattern, which is evidently how the engine finds them. All 141 fonts have every page.

**Still to confirm by rendering text against the original:** how the advance between characters is computed (width plus the bearings, or something in the undecoded bytes) and the line height.
