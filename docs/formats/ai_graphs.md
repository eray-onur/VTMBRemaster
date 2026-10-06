# AI graphs (`.ain`, `.loc`)

NPC navigation graphs, one per map, at `maps/graphs/<map>.ain` (100 files in the Steam release; every one has a matching `.bsp`). Unlike later Source games, Bloodlines stores them as text. Parser: `openbl/formats/ain.hpp`.

## Layout

The file is a stream of whitespace-separated tokens (line endings are `\r\r\n`). Lines carry no meaning: the writer wraps node records across lines and repeats a `Nodes:` label every 32 nodes, which readers skip.

```
Version	30
NumHulls:         22
UsedHullBits:     1
ZoneCount:        8
0 2 0 0 26 65 3 7
NumNodes:         102

Nodes:            3790.39,-756.67,-295.00 0.000000 -8.87 0.10 … 2 0 92274690 0 0 0
4 4 4022.39,-716.67,-295.00 …
…
TotalNumLinks      187
0 1 0 1 0 0 …
…
WCLookup:         0 1 2 3 …
```

| Part | Contents |
|---|---|
| `Version` | 30 |
| `NumHulls:` | H, the number of hull sizes (22) |
| `UsedHullBits:` | bit mask of the hulls in use |
| `ZoneCount:` | Z, then Z integers (meaning unverified) |
| `NumNodes:` | N, then N node records |
| `TotalNumLinks` | L, then L link records |
| `WCLookup:` | N integers: the editor's id for each node |

**Node record** (4 + H + ⌈N/32⌉ + 2 tokens):

| Field | Notes |
|---|---|
| origin | one token, `x,y,z` |
| yaw | degrees |
| H hull offsets | vertical offset per hull; typically one negative value, the rest 0.10 |
| type | 2 for almost every node, 1 for a few |
| info | 0 in every shipped graph |
| ⌈N/32⌉ mask words | signed 32-bit integers forming a bit set with one bit per node. A node never has its own bit set. Meaning unverified; possibly visibility or reachability. |
| zone | index into the zone list |
| link count | number of links that start or end at this node (always matches the link list) |

**Link record** (3 + H tokens): source node, destination node, flags (0, or 8192 on a few links), then one value per hull: 0 means the hull cannot use the link, 1 and 2 are probably move types such as walk and jump (unverified).

Steam release totals: 11,305 nodes and 29,075 links. Some maps have zero nodes.

## `.loc`

`maps/graphs/<map>.loc` holds a single decimal integer followed by a line break. It is neither the BSP's map revision nor a CRC32 of the `.bsp`; its meaning is unknown. It is probably used to detect a graph that is out of date with its map.
