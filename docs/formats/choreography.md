# Choreography (`.vcd`)

Scenes that time speech, facial expressions, gestures and animation sequences for one or more actors. Most sit next to a voice line, at `sound/**/<line>.vcd` (5,300 files in the Steam release); a few are cinematic scenes. Parser: `openbl/formats/vcd.hpp`.

Text in Windows-1252 that starts with `// Choreo version 1`. Tokens are quoted strings (no escapes), `{`, `}` and bare words; `//` starts a comment.

```
// Choreo version 1
actor "Andrei"
{
  bonerename "Bip01" "Bip01"
  channel "Exp1"
  {
    event expression "Joy"
    {
      time 15.453333 20.720001
      param "dialog"
      param2 "Joy"
      event_ramp
      {
        1.0480 0.9198
      }
    }
  }
}

fps 60
snap off
```

## Structure

| Where | Keyword | Arguments |
|---|---|---|
| top level | `actor` | name, then a block |
| top level | `fps` | frames per second (always 60) |
| top level | `snap` | `on` / `off` |
| actor | `channel` | name, then a block |
| actor | `bonerename` | from-bone, to-bone (4,894 actors; usually identical names) |
| actor | `faceposermodel` | authoring path to a model (1 file) |
| actor, channel | `active` | `"0"` disables it (3 occurrences) |
| channel | `event` | type, name, then a block |
| event | `time` | start and end in seconds from the start of the scene |
| event | `param`, `param2` | meaning depends on the type (below) |
| event | `fixedlength` | flag |
| event | `sequenceduration` | seconds (56 events) |
| event | `event_ramp` | block of `<time> <value>` pairs: an intensity curve over the event |

## Event types

| Type | Count | `param` | `param2` |
|---|---|---|---|
| `silence` | 12,078 | — | — |
| `loud` | 9,771 | — | — |
| `speak` | 5,363 | sound path relative to `sound/` | — |
| `expression` | 1,367 | expression set (e.g. `dialog`) | expression name (e.g. `Joy`) |
| `gesture` | 533 | gesture sequence | — |
| `sequence` | 221 | animation sequence | — |
| `firetrigger` | 22 | trigger number | — |
| `python` | 2 | Python code | — |
| `bodysound` | 1 | sound | — |

`silence` and `loud` events are probably markers of where the voice is quiet or loud. That is unverified.

## Sound references

`speak` events name a `.wav` file, but most voice lines ship as `.mp3` with the same name (5,189 of 5,363). Sound lookup therefore has to fall back from `.wav` to `.mp3`. 42 references resolve to nothing: authoring paths such as `uments and Settings\bmitsoda\Desktop\line11_col_e.wav`, or misnumbered lines.
