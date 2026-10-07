# Movies (`media/*.bik`)

The game ships four intro movies in Bink 1 format (revision `i`), each with one audio track:

| Movie | Size | Frames | Frame rate | Audio |
|---|---|---|---|---|
| `activision.bik` | 640×480 | 312 | 30 fps | 44,100 Hz stereo |
| `nvidiabloodlogo.bik` | 640×480 | 420 | 29.97 fps (500000/16683) | 48,000 Hz mono |
| `troika.bik` | 800×400 | 221 | 15 fps | 44,100 Hz stereo |
| `whitewolf.bik` | 720×480 | 661 | 29.97 fps (132200/4411) | 32,000 Hz stereo |

**Container.** `parse_bik` (`openbl/formats/bik.hpp`) reads the header:

1. `"BIK"` and a revision byte.
2. `u32` fields: file size − 8, frame count, largest frame, an unused value, width, height, frame rate numerator and denominator, flags, audio track count.
3. Per audio track, in three runs: the maximum decoded size (`u32`), then sample rate and flags (`u16` each), then the track id (`u32`).
4. The frame index: frame count + 1 offsets. Bit 0 marks a keyframe, and the last offset equals the file size.

**Decoding.** `openbl::media::VideoDecoder` (`openbl/media/video.hpp`) decodes the video and audio with FFmpeg's libavcodec, reading from memory so movies can come straight from the game archives. Frames come out as RGBA and audio as interleaved 16-bit samples. FFmpeg is LGPL and dynamically linked; see [LEGAL.md](../LEGAL.md). Build with `OPENBL_WITH_FFMPEG=OFF` to leave it out.

All four movies decode completely: every frame, and audio whose length matches the video to within 0.03 s. `openbl-cli video frame <movie> <n> <out.png>` exports a frame.
