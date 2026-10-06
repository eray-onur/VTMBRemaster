# Contributing to OpenBL

1. **Read [docs/LEGAL.md](docs/LEGAL.md) first.** Pull requests that violate the clean-room policy are closed and their history is purged.
2. Keep pull requests focused: one format, one subsystem or one fix at a time.
3. Every new file-format reader needs:
   - a spec in `docs/formats/<format>.md`, which notes which details are verified against real data and which are inferred;
   - unit tests built only from synthetic data;
   - an integration test that reads the real install through `OPENBL_GAME_DIR` and is skipped when the variable is unset.
4. Readers must never trust their input. Check every offset, size and count before using it, and report bad data with `formats::FormatError`.
5. Code style: follow `.clang-format`. The build must pass with `-DOPENBL_WARNINGS_AS_ERRORS=ON`.
6. Behaviour that differs from the original game is a bug, unless it fixes a crash or lifts a hard-coded limit. Such changes need an option that restores the original behaviour where it matters.

## Repository layout

```
cmake/         Third-party dependencies (pinned and hash-checked)
src/formats/   Readers and writers for game file formats (no engine dependencies; zlib only)
src/vfs/       Merged view of loose files and VPK archives
tools/         Command-line tools (openbl-cli)
tests/         doctest unit tests and integration tests
docs/          Roadmap, architecture, legal policy, format specs
```
