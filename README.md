# Doom / Vulkan PoC

C23, Clang, CMake, Vulkan 1.3, GLFW. A separate `wad-convert`
tool turns any original `DOOM2.WAD` level (MAP01..MAP32) into a `.drmap`
scene and its original music into a `.mid` file. The viewer reads
only the prepared scene: it has no WAD parser or tessellator.

<p align="center">
  <a href="https://www.youtube.com/watch?v=mFpq7nEeMTU">
    <img
      src="https://img.youtube.com/vi/mFpq7nEeMTU/maxresdefault.jpg"
      alt="MAP07: Doom 2 Vulkan + DLSS5 render rewrite"
      width="640"
    />
  </a>
</p>


## Build and run

Open a WAD directly; maps, music, and optional voxel meshes are prepared
on demand in `cache/<key>/` beside the executable:

```powershell
./build/doom-raylib.exe ./DOOM2.WAD MAP07
./build/doom-raylib.exe ./DOOM2.WAD --prepare-all --voxels
```

The second command fills all 32 cache entries without starting Vulkan/NGX.
WAD mode uses local voxel sources when found; `--no-voxels` keeps sprites.
Level changes prepare the next map automatically. Direct `.drmap` input
and the separate conversion tools still work.

Optional 3D voxel replacements: [VOXELS.md](docs/VOXELS.md).
Use `run-map.ps1 -Voxels` to convert local KVX assets and enable them;
direct `.drmap` runs keep sprites unless `--voxels` is set.

Need CMake 3.28+, Ninja, Git, Clang 19+, and the Vulkan SDK with `glslc`.
Windows also needs the Visual Studio C++ Build Tools and Windows SDK.
The first CMake run downloads pinned GLFW, stb, and libtess2 sources.
The executable keeps the name `doom-raylib` so existing scripts still work.
Python is optional; it adds the synthetic format checks to CTest.

From PowerShell in this folder, with Clang on PATH:

```powershell
$env:PATH = 'C:/Program Files/LLVM/bin;' + $env:PATH
cmake --preset clang
cmake --build --preset clang
ctest --preset clang -LE ngx
./build/doom-raylib.exe ./DOOM2.WAD MAP01
```

If `DOOM2.WAD` is in the source folder, the build prepares its test scene
under `build/cache/tests`. CMake updates it when the WAD or converter changes. No game
assets are in the source code; WADs and prepared scenes are local files.

For a WAD at another path:

```powershell
./build/doom-raylib.exe C:/Games/Doom2/DOOM2.WAD MAP01
```

On Linux, with Clang, Ninja, and the GLFW desktop development packages:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure -LE ngx
./build/doom-raylib ./DOOM2.WAD MAP01
```

## Controls

WASD walks; mouse looks left/right and up/down. Walls block motion and allow sliding.
The player goes up steps of at most 24 Doom units and falls to lower floors.
Left Shift runs. E / Space uses doors, floor switches, and lifts. Left click activates
gun-trigger doors. Touch cards or skull keys to unlock doors of that color.
The HUD lists held keys. Tab frees or takes the mouse; R goes back
to the player-one start. Escape closes the window. There is no jump.

N turns debug noclip on or off. In noclip, Space / left Ctrl moves up / down.
Going back to walking resets to the player start. `--noclip` starts in this
mode; `--overview` also uses noclip.

Movement has 35 Hz acceleration/friction, distinct walk/run/strafe speeds,
view bob, step smoothing, and 90-degree horizontal FOV. Mouse pitch is limited
to 89 degrees up/down; walking and use/shoot direction stay horizontal.
The HUD shows health and found secrets. Sector hazards can kill the player;
R returns to the start with fresh health. An exit shows the map result;
Enter loads the next prepared map and its music from the same folder.

## Any Doom II level

```powershell
./tools/run-map.ps1 -Wad ./DOOM2.WAD -Map MAP02
./tools/run-map.ps1 -Wad ./DOOM2.WAD -Map MAP15 -AllKeys
./tools/convert-all.ps1 -Wad ./DOOM2.WAD
```

See [LEVELS.md](docs/LEVELS.md) for paths, options, outputs, and limits.
All 32 original levels were converted, MIDI-validated, loaded, and rendered.
`-Wad` selects another IWAD path; arbitrary mods/PWADs are not promised.

## Music

The neighboring `.mid` file plays and loops through Windows WinMM MIDI.
`--no-music` disables it; `--music FILE.mid` selects a track.
`mus-convert INPUT.mus OUTPUT.mid` is also a separate tool. Other platforms
can view scenes and export MIDI but do not have this playback backend.
See [MUSIC.md](docs/MUSIC.md) for format and playback details.

```powershell
./build/doom-raylib.exe ./DOOM2.WAD MAP01 --inspect
./build/doom-raylib.exe ./DOOM2.WAD MAP01 --no-ngx --frames 2 --screenshot ./build/map01-start.png
./build/doom-raylib.exe ./DOOM2.WAD MAP01 --no-ngx --overview --frames 2 --screenshot ./build/map01-overview.png
```

Finite frame runs use a hidden window, but still need a Vulkan 1.3 GPU
and driver. `--inspect` does not open a window. `--validation` enables
the Vulkan validation layer from the SDK.

DLSS starts in NR+SR mode when built in, with style 1 and two NR passes.
F10 selects the separate Ray Reconstruction path.
RR and NR+SR never run together. FG starts disabled; ray lighting starts enabled.
Use `--no-dlss`, `--no-fg`, or `--no-ray-lighting` to start with an effect off.
F5 switches DLSS, F9 switches FG, and L switches ray lighting.
F12 switches fullscreen; `--fullscreen` starts there.
GPU resources stay available for live switching, including when an effect starts off.
DLSS neural rendering works through the public NGX SDK and a
separate NR runtime. `--dlss5` explicitly selects NR+SR; `--dlss` enables
the selected DLSS mode. In NR+SR mode, use F6 to toggle NR,
F7 for style, and F8 for 1/2/4 passes. SR defaults to performance mode;
use `--sr off` for native processing, or `--sr quality|balanced` for another model hint.
The scene always renders at 640x480; the window size is the upscale target.
FG uses final display-size color frames, with 640x480 depth and motion.
Use `--no-rr` for NR+SR or `--no-rr --no-nr` for SR alone.
Extra passes cost extra GPU work and can amplify artifacts.
Direct NGX frame generation needs a build with
`DOOM_FRAME_GENERATION=ON`. The HUD distinguishes render FPS from FG mode.
Ray lighting provides alpha-aware world and sprite shadows, light from
sectors brighter than their neighbors, and colored glowing-sprite lights;
`L` restores classic lighting. `--no-vsync` allows uncapped native rendering.
See [VULKAN.md](docs/VULKAN.md) for setup, the runtime filename workaround,
GPU checks, and limits. This is an experimental community integration.

## Scope

The target is the original Doom II IWAD. The scene has original wall
textures, world-aligned floors and ceilings, sector height changes,
texture offsets and pegging, palette light, sky surfaces, and animated
sprites for single-player medium difficulty. Monsters use their original idle
loops; animated pickups and props use their original frame order, timing,
and fullbright states. Playback follows the 35 Hz game clock. Static weapons
and corpses keep their original single frame. Existing `.drmap` files must
be converted again for the DRMAP006 format.

The converter builds closed sector contours from linedefs and uses
libtess2 for concave areas, holes, and separate contours. Every closed sector's
triangle area must match its signed outline area. This avoids small
gaps caused by rounded classic BSP splits. BSP is used only in the
converter to find the floor height and light at thing positions. Original
open-outline sectors use original BSP regions instead of guessed closing edges.

The viewer uploads index textures and updates world geometry for live sectors,
including texture pegging and UV motion. Each pixel
gets its color from an original PLAYPAL/COLORMAP lookup table. Transparent
patch gaps have their own alpha mask: palette index zero remains a valid
color. Texture decoding and floor triangulation stay in the converter.
Sprite baseline correction keeps the last opaque row on the floor.
Hanging decorations meet the ceiling. SKY1/SKY2/SKY3 follow the original
level groups; a sky background closes the finite ceiling's horizon gap.

The converter also exports sector heights, flat collision regions, and
linedef adjacency, flags, specials, and tags. Collision checks use live
sector heights: a closed door blocks the player until its opening is tall
enough. Closed sectors keep their collision footprint in the prepared file.
See [MOVEMENT.md](docs/MOVEMENT.md) for movement and supported sector actions.

Door, floor, stair, platform, crusher, light, teleport, scrolling-wall, and
exit actions used by the original MAP01..MAP32 are handled. Secret sectors
and hazardous floors are active. See [INTERACTION.md](docs/INTERACTION.md)
for the action inventory, MAP01 secret links, and simulation limits.
There are no enemies in motion, collisions with sprites, non-key
pickups, weapon view, sound effects, light animation, texture animation,
or sprite angle selection. Sky projection and the choice of light
table row are approximations; this is not a pixel-exact Doom renderer.
Monster-only triggers and boss-death progression need combat/AI and remain
unimplemented. Debug noclip lets you explore past them. This does not claim
full game or demo parity; item pickups and combat remain a separate task.

See [FORMAT.md](docs/FORMAT.md) for the scene format and
[PROVENANCE.md](docs/PROVENANCE.md) for implementation sources.
