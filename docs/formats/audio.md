# Audio (`.wav`, `.mp3`)

Sounds live under `sound/`. Parser: `openbl/formats/wav.hpp`. `openbl-cli audio export <sound> <out.wav>` writes any WAV sound as plain 16-bit PCM for listening.

## WAV

3,922 files; 15 are empty placeholders (0 bytes, e.g. the clinic ambience). By encoding:

| Encoding | Files |
|---|---|
| Microsoft ADPCM (format 2), 4-bit, mono, 22,050 Hz | 2,899 |
| Microsoft ADPCM, mono, 44,100 Hz | 562 |
| Microsoft ADPCM, stereo, 44,100 Hz | 361 |
| Microsoft ADPCM, stereo, 22,050 Hz | 2 |
| PCM 16-bit (mono or stereo, 22/44 kHz) | 82 |
| PCM 8-bit, 11,025 Hz | 1 |

**Microsoft ADPCM** is a published Microsoft multimedia standard, implemented from that description. Each block starts with, per channel, a predictor index, a step size and two seed samples. 4-bit codes follow, high nibble first and channels interleaved. Each sample is predicted from the previous two using a coefficient pair from the file's format header, and the step size adapts after every sample. Most files carry a `fact` chunk with the true sample count; the last block is padded and is trimmed to it. 145 files declare one sample more than their final partial block holds, an encoder quirk.

All 3,907 non-empty files decode: about 371 minutes. Decoded output is smooth (sample-to-sample change about 0.2 of the signal level, against about 1.4 for noise) and never clips.

Other chunks seen: `bext`, `minf`, `elm1`, `elmo` (editor metadata; ignored).

## MP3

5,171 files, mostly voice lines: MPEG-1 Layer III at 44,100 Hz, 5,121 of them mono, about 861 minutes in all. `decode_mp3` (`openbl/formats/mp3.hpp`) decodes them with minimp3, a public-domain (CC0) single-header decoder fetched at build time. All 5,171 decode, and all but one pass the same smoothness check as the WAV files. The exception is Skelter's whispered line (`dlg/downtown la/skelter/whisper.mp3`), which really is noise-like. `openbl-cli audio export` writes either kind to a PCM WAV.

## Editor leftovers

`.sfk` (Sound Forge peak files, signature `SFPK`) and `.pk` (another audio editor's peak files) store waveform overviews for audio editors. The game does not use them.
