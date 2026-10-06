# Lip-sync (`.lip`)

One `.lip` file sits next to each spoken sound, at `sound/**/<name>.lip` (5,441 files in the Steam release). It holds the spoken text, the timing of each word and phoneme, the closed caption, and playback options. Parser: `openbl/formats/lip.hpp`.

The file is text, either CRLF or LF, but the caption is raw UTF-16LE embedded in it (see below). Read it as bytes.

```
VERSION 1.2
PLAINTEXT
{
Check...check. Is this thing recording?
}
WORDS
{
WORD Check 0.064 0.272
{
679 ch 0.064 0.144 1.000 0
603 eh 0.144 0.256 1.000 0
107 k 0.256 0.272 1.000 0
}
}
EMPHASIS
{
}
CLOSECAPTION
{
english
{
PHRASE unicode 42 <42 bytes of UTF-16LE> 0.080 2.112
}
}
OPTIONS
{
voice_duck 1
speaker_name answering machine
}
```

## Sections

All five sections appear in every shipped file, in this order.

- **`VERSION`**: `1.0` (154 files), `1.1` (487) or `1.2` (4,800).
- **`PLAINTEXT`**: the transcript, Windows-1252.
- **`WORDS`**: one `WORD <text> <start> <end>` per word, with times in seconds, followed by a block of phonemes. Each phoneme line is `<code> <name> <start> <end> <weight> [<flags>]`:
  - `code` is the phoneme's IPA code point in decimal: 679 = U+02A7 `ʧ` ("ch"), 603 = U+025B `ɛ` ("eh"), 100 = `d`.
  - `name` is an ARPAbet-style label. In a few hundred lines it disagrees with `code`; which of the two the original engine uses is unverified.
  - `weight` is 1.000 in every shipped file.
  - `flags` is missing in older files (11,228 lines). Where present it is 0, or 1 on 668 lines. Its meaning is unknown.
- **`EMPHASIS`**: empty in every shipped file.
- **`CLOSECAPTION`**: a language name (always `english`), then a block of `PHRASE <encoding> <byte count> <text> <start> <end>` lines. The encoding is always `unicode`, and the text is exactly `byte count` bytes of UTF-16LE. Those bytes can include `0x0a`, so the phrase must be read by byte count and not by line. Each shipped file has one phrase; 65 of them use characters outside Latin-1, such as curly quotes.
- **`OPTIONS`**: `key value` pairs. `voice_duck 0|1` and `speaker_name <name, may contain spaces>` are the ones seen so far.

Steam release totals: 111,469 words and 357,640 phonemes. 58 words end before they start, which looks like authoring noise.
