# Collision models (`.phy`)

Every model with physics has a `.phy` next to its `.mdl` (2,854 in the Steam release). It holds one or more **solids**, each made of convex pieces, followed by a KeyValues text block. Props have one solid; ragdolls have one per bone. Parser: `openbl/formats/phy.hpp`. The layout was worked out from the shipped files; all 2,854 parse.

All integers are little-endian. Offsets marked "rel" are relative to the start of the structure that holds them, and always stay inside their solid.

## Header (16 bytes)

| Offset | Field |
|---|---|
| 0 | `i32` header size, 16 |
| 4 | `i32` 0 |
| 8 | `i32` solid count |
| 12 | `i32` checksum: equals the model's checksum, except in 35 stale files |

## Solids

Each solid is an `i32` byte size followed by that many bytes:

| Offset | Field |
|---|---|
| +0 | `f32[3]` mass centre (physics space) |
| +12 | `f32[3]` rotational inertia |
| +24 | `f32` bounding radius |
| +28 | `i32` packed byte size and maximum deviation |
| +32 | `i32` offset (rel) of the bounding tree |
| +36 | 8 unused bytes |
| +44 | `"IVPS"`, or zero in ragdoll solids |
| +48 | convex pieces, then the bounding tree |

**Bounding tree.** 28-byte nodes: `i32 right child (rel)`, `i32 piece (rel)`, `f32 centre[3]`, `f32 radius`, `u8 box sizes[3]`, `u8`. A node with a right child is internal, and its left child follows it immediately. A node without one is a leaf that points at a convex piece. Walk the tree to find the pieces. Walking the pieces in sequence does not work, because some solids keep their points in a shared pool and the size field in the piece header then does not give the stride.

**Convex piece** (16-byte header): `i32 points offset (rel)`, `i32` client data, `u32` flags (bits 8–31: size ÷ 16 when the points follow the piece), `u16 triangle count`, `u16`. It is followed by 16-byte triangles: `u32` index and flags, then three `u32` edges whose low 16 bits are the edge's start point. Points are `f32[4]`, in metres.

**Coordinates.** Physics space is metres with different axes. Model space (inches) is `(x, z, −y) / 0.0254`. For single-solid props with float vertices, the collision bounds then match the model's vertex bounds to a median of 0.4%. Props with quantized vertices match to a median of 6%: their hulls may be coarser, or the quantized positions slightly off. Ragdoll solids are in their bone's space; this is unverified.

Totals: 6,785 solids, 7,889 convex pieces, 301,216 triangles.

**MOPP solids** (map collision only, tag `MOPP` at +44): pieces of two triangles follow the header back to back, each sized exactly 16 + 16 × triangles, up to a shared point pool. The tree offset points at compiled MOPP code after the pool.

## Text block

After the last solid comes KeyValues text, ending in a zero byte:

```
solid {
"index" "0"
"name" "ac1_r"
"mass" "999.999939"
"surfaceprop" ""
"damping" "0.000000"
"rotdamping" "0.000000"
"inertia" "1.000000"
"volume" "1644600.250000"
}
editparams {
"rootname" ""
"totalmass" "1000.000000"
"concave" "1"
}
```

Ragdolls have one `solid` block per bone (its `name` is the bone name) plus constraint blocks between them. Two files produce KeyValues diagnostics.
