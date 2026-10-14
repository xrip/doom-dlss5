# DRMAP005

Version five is a compact, sequential scene file. All integers are
little-endian. Floats are IEEE-754 binary32, written in little-endian
order. Names are eight bytes, padded with zero bytes. There are no
struct dumps, native pointers, WAD records, or BSP nodes in the file.

Coordinates use 32 Doom units per world unit. Doom `(x, y, height)`
becomes `(x / 32, height / 32, -y / 32)`. Yaw is in radians, with zero
looking along world +X and positive yaw turning toward world -Z.

## Header

| Field | Size |
| --- | --- |
| Magic `DRMAP005` | 8 bytes |
| Map name | 8 bytes |
| Asset count, batch count, sprite count | 3 x uint32 |
| Spawn x, y, z and yaw | 4 x float32 |
| Lighting LUT | 32 x 256 x RGBA8 |

The LUT is `PLAYPAL[COLORMAP[shade][index]]`, with alpha 255.

## Assets

For each asset: name (8 bytes), width and height (uint32), left and top
patch offsets (int32), then `width * height * 4` bytes of pixels.

Pixels are row-major RGBA8. R holds the palette index, A holds coverage
(0 or 255), and G/B are zero. Wall textures are already composed from
patches. Flats and sprite patches use the same pixel layout. Dimensions
are kept for UV scale and sprite size; patch offsets place sprites at
their original origin.

## Mesh batches

For each batch: asset index, shade (0..31), sky flag (0 or 1), and vertex
count, then owner and plane, all uint32. Then one
`(x, y, z, u, v)` float32 record per vertex.
Each three vertices form a triangle. Batches group surfaces by texture,
light, and sky mode. Plane 0 is wall, 1 floor, 2 ceiling, 3 switch. Owner is
a sector ID for planes 1/2 and a linedef ID for plane 3. Closed sectors
keep flat meshes, drawn only when their opening has positive height.
Both sides are drawn; no index array is needed.

Floor/ceiling UVs come from map coordinates, so mesh splits do not move
the pattern. Wall UVs already include sidedef offsets and pegging.
Masked middle walls are clipped to their single vertical texture span.
Sky batches reference the composed SKY1/SKY2/SKY3 texture and use view direction
in the shader rather than ordinary UVs.

## Sprites

For each sprite: asset index, shade, owner sector, and flags
(4 x uint32), then world x, y, z (3 x float32), 28 bytes total.
Flag bit 0 is the ceiling anchor; bit 1 is fullbright. Other bits are rejected.
The owner lets
floor motion carry grounded sprites without changing hanging sprites.
The viewer uses a vertical quad with the asset's patch offsets
and turns it around the vertical axis with the camera. Only one static
patch per type is stored in this version. Grounded positions are corrected
so the last opaque patch row meets the floor. Hanging sprites use ceiling
height. Runtime pickup flags are not serialized.

## Collision data

After sprites: sector count, line count, and region count (3 x uint32).

Each sector stores floor and ceiling heights (2 x float32), followed by
its original special, tag, light level (0..255), floor asset, and ceiling
asset (5 x uint32), 28 bytes total. `UINT32_MAX` is the sky ceiling asset.
Array indices are stable sector
IDs. The runtime uses this array as the current heights; a door or
lift can change them without rebuilding collision lines or regions.

Each line stores start x/z and end x/z (4 x float32), followed by front
sector, back sector, original flags, special, and tag (5 x uint32).
`UINT32_MAX` means no sector on that side. Sector IDs and activation data
are preserved even for closed doors. The player uses the impassable flag;
monster-only blocking and texture opacity do not block the player.

Each region stores three x/z pairs (6 x float32), then a sector ID
(uint32). Regions describe the complete sector footprint, including
sectors whose starting floor equals their ceiling. Zero-area triangles
are omitted. Region heights come from sectors, not render mesh vertices.

## Wall bindings

After regions: wall count (uint32), then batch ID, first vertex, linedef ID,
side, kind, alternate switch asset (6 x uint32), and X/Y sidedef offsets
(2 x float32) per wall. Each wall owns six consecutive mesh vertices.
Kinds 0/1/2 are middle/upper/lower. `UINT32_MAX` means no switch alternative.
Current sector heights and these bindings produce animated positions and
UVs without WAD reads. Door timers are runtime state, not serialized.
Music is a separate neighboring `.mid` file.

## Teleport destinations

After wall bindings: destination count (uint32), then position x/y/z and
yaw (4 x float32), and owner sector (uint32), 20 bytes per destination.
These are the original type-14 things, in WAD thing order. The destination's
current sector floor supplies the player height when teleporting. No WAD
thing records or lookup are needed at runtime.

Older files are rejected with a request to run `wad-convert` again.
CMake regenerates the local MAP01 scene when the converter changes.

The reader checks dimensions, references, counts, finite coordinates,
truncation, and trailing bytes before any GPU resources are made.
The complete file must be at most 512 MiB. There is no compression.
