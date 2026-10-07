# Text data tables (`vdata`, particles, sound schemes, UI)

Most of the game's tunable data is KeyValues text (see [materials.md](materials.md) for the syntax). This page lists where it lives, what it holds, and the quirks found by parsing every file. The sweep test is `tests/test_text_data.cpp`.

## Where the data is

| Folder | Files | Root key | Contents |
|---|---|---|---|
| `vdata/items/` | 216 | `WeaponData` | items and weapons: models, names, descriptions, inventory sprites, sounds, stats |
| `vdata/signs/` | 256 | `SignData`, `NewspaperData` | readable signs, newspapers, tutorial pop-ups |
| `vdata/hackterminals/` | 52 | `TerminalDefinition` | computer terminals: log-on screen, directories, e-mails, functions |
| `vdata/camerashots/` | 66 | `CameraShotTable` | dialogue camera shots |
| `vdata/precache/` | 1 | `PreCacheData` | assets to load up front |
| `vdata/system/` | 88 | various | core game rules (below) |
| `particles/` | 1,594 | `Particle` | particle effects |
| `sound/schemes/` | 147 | `SoundScheme` | per-area music and ambience |
| `resource/*.res`, `scripts/*.res` | 25 | panel names | UI layouts and colour schemes |
| `resource/gameui_english.txt` | 1 | `lang` | UI strings, UTF-16 (below) |

### `vdata/system/`

These tables define the game's rules. Phase 2's data-driven registries are built from them.

| File(s) | Root key | Contents |
|---|---|---|
| `clandoc000.txt` | `ClanDataTables` | clans |
| `npctemplate*.txt` (35) | `ClanDataTables` | NPC stat templates |
| `disciplinetgt_000`–`004.txt` | `DisciplineTgtList` | discipline effects on targets |
| `feats.txt` | `FeatData` | feats (skills) |
| `stats.txt` | `StatData` | attributes, abilities, disciplines |
| `rules.txt`, `rules_tables.txt` | `RuleData` | combat and skill rules |
| `dicerolls.txt` | `DiceRollData` | dice rolls |
| `traiteffect.txt`, `traiteffects000.txt` | `TraitEffectsData` | trait effects |
| `levelingtemplate_000.txt` | `LevelingTemplateList` | NPC levelling |
| `charcreatewizard.txt`, `chareditor.txt` | `CharCreateWizard`, `CharEditor` | character creation questionnaire and editor |
| `histories000.txt`, `history.txt` | `HistoryDataTables`, `HistoryData` | character histories |
| `items.txt` | `ItemTypeData` | item types |
| `vendors.txt` | `VendorData` | shop inventories |
| `quests_*.txt` (5) | `QuestTable` | quest log text and states |
| `dispositiontable.txt` | `DispositionTable` | NPC moods: facial expressions, blinking, eyes |
| `reaction.txt`, `reactions000.txt` | `ReactionsData` | NPC reactions |
| `stealth.txt`, `stealthkillrules.txt` | `StealthData`, `StealthKillRules` | stealth |
| `strings.txt`, `strings_internal.txt` | `StringData` | game strings |
| `infobartypes.txt` | `InfoBarMessages` | HUD messages |
| `interestingplacetypelist.txt` | `InterestingPlaceTypeList` | NPC points of interest |
| `loadingtips.txt`, `mapnames_localized.txt`, `keynames.txt`, `engineerrors.txt` | various | UI text |
| `newscaster_main.txt`, `newscaster_side.txt`, `radio_data.txt` | `NewsData`, `RadioData` | radio and TV |
| `sndscheme_*.txt` (5), `sound_volume_table.txt` | `SoundSchemeTables`, `SoundVolumeTable` | sound sets |
| `particleimpacttable.txt` | `ParticleImpactTable` | impact effects per surface |
| `dialog.txt` | `DialogData` | dialogue settings |
| `experience_table.txt` | — | **not KeyValues**: own format, `>` starts a comment |
| `credits.txt` | — | **not KeyValues**: credits script, `#` comments and `@font` commands |
| `charaction_sounds.txt` | — | designer note, not data |

## Syntax used by these files beyond materials

- **`/* … */` block comments**, used in particle files to disable sections.
- **`\"` inside quoted strings**, used in 8 places (signs, hacking terminals) to quote words in text. The parser only honours it when `KvOptions::escaped_quotes` is set, which vdata loaders do. Materials must not use it, because their paths contain backslashes.

## Damage in the shipped files

The parser recovers from all of the following and reports each one as a diagnostic.

- **Missing closing quote** (14 files, e.g. `"Filename" "environmental/seafront/buoy.wav` at the end of a line, `"MinBlinkInterval  "2.5"`, and the unclosed text of Bach's journal). When a closing quote is directly followed by a word character, it was really the next opening quote. The string then ends at its last line break if only whitespace follows that break, which keeps multi-line text; otherwise it ends at its first tab.
- **Missing final `}`** (dozens of particles, 11 hacking terminals, `disciplinetgt_000`, `npctemplate020`): closed at end of file.
- **Extra `}`** (`traiteffects000`, one particle): ignored.
- **Block whose key was commented out** (`//	spawn` followed by `{ … }`, 5 particles): dropped. This was probably meant to disable the spawner.

## Localisation (`resource/gameui_<language>.txt`)

UTF-16LE with a byte-order mark, containing KeyValues of the form `"lang" { "Language" "English" "Tokens" { "Name" "Text" … } }`. Text uses `\n` for a line break. The English file has 192 tokens, and `GameUI_Advanced` is defined twice (the first definition wins). Parser: `openbl/formats/localization.hpp`. It also accepts UTF-8 and Windows-1252 files, for mods.

## Line-based formats

Parser: `openbl/formats/script_text.hpp`.

| File | Format | Release |
|---|---|---|
| `vdata/system/experience_table.txt` | `Title \| Description \| XP` rows; `>` starts a comment, and comments name the quest each group belongs to. Only the middle field is localised. | 167 rows |
| `vdata/system/credits.txt` (also played by `PlayEndCredits`) | a line script: `#` comment, `@font`, `$r g b a` text colour, `%r g b a` drop shadow, `^x y w h` credit box (fractions of the screen); every other line is text | 804 lines |
| `scripts/sentences.txt` | `NAME sound/path {options}`; names ending in a number form groups the engine picks from (`SPI_DIES0`, `SPI_DIES1`); options such as `Len 1.50` | 18 sentences, all sounds present |
| `scripts/titles.txt` | `$name value` settings (position, effect, colours, fade times) followed by `NAME { text }` blocks that use the settings in effect | 13 titles, all test text left over from the engine |
| `scripts/dsp_presets.txt` | `{ index type mix-min mix-max duration fade dbmin mixdrop { processor } … }`; processors are a code (`RVA` reverb, `DFR` diffusor, …) and numbers | 141 presets |

One shipped flaw: the column header at line 765 of `dsp_presets.txt` lacks its `//` and is skipped.

Facial expression tables (`expressions/*.txt`) have their own page: [expressions.md](expressions.md).
