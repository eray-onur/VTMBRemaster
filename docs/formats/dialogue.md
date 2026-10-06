# Dialogue (`.dlg`)

Conversation trees, one file per NPC or conversation, in `dlg/<area>/<name>.dlg` (138 files in the Steam release). Plain text in Windows-1252 with CRLF line endings. Parser: `openbl/formats/dlg.hpp`.

## Records

Each line is a record of 13 fields. Every field is wrapped in braces, and fields are padded with tabs:

```
{	11	}{	[Tortured]They get you too?	}{	[Tortured]They get you too?	}{	#	}{	npc.SetDisposition("Anger", 1)	}{	pc.SetQuest("Barabus",2)	}{	}{	}{	}{	}{	}{	}{		}
{	12	}{	Barrabus, I presume.	}{	Barrabus, I presume.	}{	21	}{		}{		}{	}{	}{	}{	}{	}{	}{	You are Gary's Chinatown eyes?	}
```

| Column | Meaning |
|---|---|
| 0 | Index. Normally equal to the record's 1-based position in the file. |
| 1 | Text for a male player. On an NPC line, what the NPC says to a male player; on a reply, what the male player says. NPC text may start with an acting direction in `[brackets]`. |
| 2 | Text for a female player (same rules). |
| 3 | `#` marks an NPC line. On a player reply: the index of the NPC line it leads to, `0` to end the conversation, or occasionally a script expression (`RandomLine([571, 561])`). Empty on placeholder rows. |
| 4 | Condition. On replies, a Python expression deciding whether the reply is offered, with some extra forms: `Humanity -5`, `IsClan(pc,"Nosferatu")`, `Haggle 3; pc.CurrentMoney() >= 100`. On NPC lines it mostly holds statements such as `npc.SetDisposition("Anger", 3)` or `pc.SetCamera(...)`; how the original treats those is unverified. |
| 5 | Action: Python statements run when the line is chosen or spoken. |
| 6–11 | Unused. Empty in shipped files apart from one designer note. |
| 12 | Malkavian variant of the text. Used on replies (8,585) and some NPC lines (68). Empty means the normal text is used. |

Which replies belong to an NPC line is not stored. Replies are the records that follow an NPC line up to the next NPC line. Indices are usually laid out in blocks of ten (NPC line 11, replies 12–15), with empty placeholder rows in between.

## Quirks in the shipped files

The parser accepts all of these and reports them as diagnostics:

- **12 fields** (620 records): the Malkavian column was left off.
- **Spreadsheet quoting** (`doll1`–`doll4`, `vandal`): fields that held quotes or line breaks were wrapped in `"`, with inner quotes doubled. They are unwrapped only when they show those signs, so ordinary quoted speech is kept as is.
- **Missing `}`** (`arthur.dlg` record 1011, `skelter.dlg` record 815): the next `{` closes the field. If a line break came first, it also ends the record.
- **Index mismatches** (64 records, e.g. `mingxiao2.dlg` has rows without an index): the index is kept as written.
- **Dangling links** (3): replies that point at an index which is not an NPC line.

Steam release totals: 49,736 records, of which 5,563 are NPC lines and 13,139 are player replies, with 10,350 links between them.
