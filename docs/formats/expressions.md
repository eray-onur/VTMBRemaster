# Facial expressions (`expressions/*.txt`, `.vfe`)

Each character has an expression set (`<name>_expressions`) and a phoneme set (`<name>_phonemes`). A set names a list of flex keys (facial controls such as `right_lid_raiser` or `jaw_drop`) and defines each expression or phoneme as a value and a weight per key. Every set ships twice, as an editable table (`.txt`) and in compiled form (`.vfe`), with 249 of each in the Steam release. Parser: `openbl/formats/expressions.hpp`.

Choreography `expression` events name an expression (see [choreography.md](choreography.md)), and lip-sync phoneme codes select phoneme rows (see [lipsync.md](lipsync.md)).

## Table (`.txt`)

```
$keys right_lid_raiser left_lid_raiser … lower_lip
$hasweighting
"Neutral" "_" 0.000 0.000 0.000 0.000 … "all zeros, boyee"
"Joy" "_" 0.000 0.000 0.000 0.000 0.390 1.000 … "Debauched Smile"
```

- `$keys` lists the flex keys, from 0 to 44 of them depending on the set.
- `$hasweighting` means each key has a value and a weight; without it, each key has a value only (weight 1). Every shipped table has it.
- Each row is: name, code, then a value and weight per key, then a description.
  - In expression sets the code is always `_`.
  - In phoneme sets the code is a single character (`b`) or a code point in hex (`0x0279`, IPA `ɹ`). It matches the phoneme codes in `.lip` files.
- Values are written with three decimals.

## Compiled (`.vfe`)

Little-endian.

| Offset | Contents |
|---|---|
| 0 | `EFV\0` |
| 4 | int32 version, 0 |
| 8 | the set's own path (`expressions/adam_expressions.vfe`), zero-padded to 128 bytes. Two files (`phonemes_strong`, `phonemes_weak`) use 64 bytes. Find the header by checking which of 0x88 and 0x48 holds the file length. |
| header + 0 | int32 file length |
| +4 | setting count |
| +8 | offset of the setting records |
| +12 | 0 |
| +16 | index count: 96 for expression sets, 953 for phoneme sets (one more than the largest code, IPA `θ` = 952) |
| +20 | offset of the index: int32 per code, the last setting with that code, or −1 |
| +24 | key count |
| +28 | offset of the key name table: int32 absolute offset of each key's name |
| +32 | offset of the key mapping: int32 per key, −1 on disk (presumably filled in at load time with the model's flex controller) |

A **setting record** is 24 bytes: name offset (relative to the record), 0, entry count, code, 0, entry offset (relative to the record). **Entries** are 12 bytes, `{int32 key, float value, float weight}`, and are stored only for keys whose value is not zero. Descriptions are not compiled in. Strings are zero-terminated.

## Tables against compiled sets

Values in the tables are rounded to three decimals, while the compiled sets keep full precision (0.182 against 0.18239999). Allowing for that rounding, 241 of the 248 pairs match. The other seven tables were edited after their last compile; `larry_phonemes.txt`, for example, has six keys that its `.vfe` lacks:

`larry_phonemes`, `luca_phonemes`, `newscaster_male_expressions`, `pisha_expressions`, `shu_phonemes`, `stripper_expressions`, `sweeper_phonemes`.

The compiled sets are what the game ships, so OpenBL loads them and treats the tables as source material.
