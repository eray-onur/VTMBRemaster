# Clean-room policy

OpenBL survives only if its code is demonstrably our own. Bloodlines runs on an early, pre-release branch of Valve's Source engine, and Valve's source code has leaked more than once. This policy keeps the project clear of all of it.

## Forbidden sources

Contributors must **never view, copy or derive from** any of the following:

- The leaked Source engine code of 2003, the leaked CS:GO/TF2 code of 2020, or any other leaked Valve or Troika source code.
- The Source SDK or Source SDK 2013. Their license limits use to Source-engine products.
- Decompiled code from `vampire.exe`, `engine.dll`, `vampire.dll`, `client.dll` or any other original binary, pasted or transcribed into this repository.

If you have studied any of the leaked code, do not contribute to the subsystem it covers. If in doubt, say so in your pull request, and a maintainer will help you find a part of the project you can safely work on.

## Allowed sources

- **Observing the shipped game**: playing it, logging its behaviour, capturing frames, measuring timings, diffing save files.
- **Analysing data files** to work out their layout for interoperability, then documenting that layout in prose under `docs/formats/`.
- **Black-box binary analysis to understand behaviour.** The person doing the analysis writes a plain-language spec (what happens, not how the code is written). A different contributor implements from that spec. Specs must not contain decompiled code or pseudo-code transcribed from it.
- Public, independently written documentation and community tools, if their licenses allow it. Name the source in the spec.
- Original Python scripts from the game's data. These are game data that the engine runs. They must not be copied into this repository.

## Game data

- Never commit game files or anything extracted from them. This includes test fixtures, screenshots of decoded textures used as references, and dialogue text. `.gitignore` blocks the common extensions.
- Tests build synthetic files from scratch, or read the user's own install through `OPENBL_GAME_DIR`.
- OpenBL release builds ship only the engine. They read data from a user-supplied installation.

## Names and trademarks

"Vampire: The Masquerade", "Bloodlines" and "World of Darkness" are trademarks of their respective owners. Use them only to describe compatibility ("runs Bloodlines game data"). Never use them as the name of OpenBL, its binaries or its logo.
