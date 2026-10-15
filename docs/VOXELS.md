# Optional voxel models

Voxel models are optional; direct `.drmap` runs use sprites by default.
Models use the existing Vulkan geometry,
sector shade, ray lighting, shadows, motion vectors, and RR material outputs.
Collision, pickups, triggers, and sprite animation timing do not change.

## Use

For automatic preparation and cache reuse:

```powershell
./build/doom-raylib.exe ./DOOM2.WAD MAP07
./build/doom-raylib.exe ./DOOM2.WAD --prepare-all --voxels
```

WAD mode enables voxels when the local sources are present. Prepared `.drmap`
input keeps the explicit `--voxels` opt-in. Automatic conversion writes only
to the executable's `cache/<key>/`; level changes use the same source/cache.

With your local `voxels/`, `VOXELDEF1.txt`, and `voxeldef.txt`:

```powershell
./tools/run-map.ps1 -Wad ./DOOM2.WAD -Map MAP01 -Voxels
./tools/convert-all.ps1 -Wad ./DOOM2.WAD -Voxels
./build/doom-raylib.exe ./build/cache/manual/MAP07.drmap --voxels
```

The launcher converts the map before starting it. `-Voxels` reads the two
local definition files in the order shown above; later entries win.
Direct conversion can use other definition files:

```powershell
New-Item -ItemType Directory -Force ./build/cache/manual | Out-Null
./build/wad-convert.exe ./DOOM2.WAD ./build/cache/manual/MAP01.drmap MAP01 --voxels ./voxels --voxeldef ./VOXELDEF1.txt --voxeldef ./voxeldef.txt
./build/doom-raylib.exe ./build/cache/manual/MAP01.drmap --voxels
./build/doom-raylib.exe ./build/cache/manual/MAP01.drmap --inspect --voxels
```

Conversion writes `MAP01.drvox` beside the unchanged `MAP01.drmap` and
music. `--voxels` loads that map's sidecar, including after a level exit.
A missing or mismatched sidecar is an error. Without `--voxels`, the viewer
does not read it and keeps sprites.

Unmapped models stay as sprites. A missing animation frame keeps the whole
object on sprites rather than switching between 2D and 3D each tick.
Missing source models are reported during conversion. Source packs,
definitions, WADs, and generated sidecars are not distributed with the
engine. Their separate asset licenses still apply.

## Conversion and placement

- Reads the highest-detail KVX mip, exposed-face bits, signed 24.8 pivot,
  and trailing 6-bit RGB palette. Other mips are not used.
- Merges adjacent equal-color exposed faces into rectangles. Hidden faces
  and empty space between surface runs produce no geometry.
- Remaps KVX RGB to the nearest base WAD palette color offline. This is
  lossy, but keeps the existing sector shade LUT.
- Applies `Scale / 32`, the pivot, and resolved `AngleOffset + 90`.
  Raw axes become `(x - pivotX, pivotZ - z, y - pivotY)` before yaw.
- Saves WAD thing yaw. Models rotate with the thing, not the camera.
  `PlacedSpin` subtracts degrees per second at the existing 35 Hz tick.
- Uses the live sector floor or ceiling as the anchor, without sprite
  patch baseline correction.

Definition keys have four characters for all frames or five for one frame.
Multiple keys can share an assignment. Numeric options are `Scale`,
`AngleOffset`, `Spin`, `PlacedSpin`, and `DroppedSpin`. Dropped objects do
not yet exist, so `DroppedSpin` has no runtime effect. `UseActorPitch`,
`UseActorRoll`, and `PitchFromMomentum` are accepted but have no effect
while actors have no pitch, roll, or momentum state. Other options are
rejected, not silently approximated.

## Lighting and limits

Each frame uses its prepared mesh and the existing object vertex buffer.
Faces have geometric normals and opaque palette materials; holes are empty
geometry, not the sprite alpha mask. Models cast and receive ray shadows,
including self-shadowing. Glowing objects keep their existing light source;
rays skip the emitter itself. Models receive surface lighting even when
the sprite frame is fullbright. Classic mode still uses that frame's shade.

RR gets model motion, linear depth, diffuse albedo, and world normals.
Materials remain rough diffuse: roughness one, specular albedo zero.
No separate voxel lighting or AI pass is added.

Actors share local pose meshes in one device-local vertex buffer. The GPU
applies current/previous object matrices for rendering and motion; animation
selects a prepared mesh rather than rewriting vertices. Ray queries reuse
one static BLAS per pose with actor transforms in a small TLAS. Only moving
world geometry needs BLAS refits. Both sprite quads and models use this path.

This removes per-actor mesh copies and the previous whole-level refit on
camera/animation changes. Native packs have an eight-million-vertex bound;
the shared vertex pool has a sixteen-million-vertex bound. Memory grows
with unique frame meshes, not with a full mesh copy for each actor. New
combat animations are outside this feature.

## Native sidecar

Integers and floats are little-endian 32-bit values; names are eight bytes
padded with zeros. `DRMAP006` is unchanged.

1. Eight-byte `DRVOX001` magic, map name, original asset count, sprite count.
2. Per original asset: name, vertex count, spin in degrees/second, followed
   by triangle-list vertices `(x, y, z, u, v)` as five floats.
3. Per sprite: original asset index and WAD yaw in radians.

UVs select a palette index in a generated opaque 256x1 indexed texture.
The loader checks map, counts, asset names, sprite assets, mesh ranges,
finite values, and file end. Regenerate after changing the WAD or
definitions; these checks are not a content hash.

Regenerate packs made before the facing fix: their meshes already contain
the old mirrored Y axis. The definition's default angle is still 90 degrees
internally; explicit `AngleOffset = 0` has the same result as no option.

Format references: [PROVENANCE.md](PROVENANCE.md).
