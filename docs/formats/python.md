# Python scripts (`python/`)

The game's level logic is Python: one module per area (`santamonica`, `chinatown`, `hollywood`, …), plus `vamputil` (shared helpers), `levelscript`, and copies of two standard library modules (`lib/random`, `lib/string`). Dialogue conditions and actions (`.dlg`), entity outputs and choreography events also hold short Python snippets that run in the same interpreter.

## Version

All 24 compiled files (`.pyc`) start with magic number 60202 followed by `\r\n`: **Python 2.1** (2.0 is 50823, 2.2 is 60717). The engine embeds that interpreter.

## Sources are shipped

All 24 `.pyc` files have their `.py` source next to them, and 8 more `.py` files have no compiled copy (32 sources in all). So the scripts can be read and run from source; nothing needs decompiling. Following [LEGAL.md](../LEGAL.md), they are game data: OpenBL runs them from the user's install and never copies them into this repository.

## What this means for Phase 2

The scripting host must run **Python 2.1-era code** unmodified. That code uses `print` statements, `string`-module functions, integer division with `/`, and builds before `True`/`False` existed. The Python state in saves is pickled with protocol 0 (see [saves.md](saves.md)). Options for the host, to be decided in Phase 2:

- embed a modern Python 3 with a source translator and compatibility layer;
- embed a Python 2 interpreter;
- write a purpose-built interpreter for the subset the game uses.
