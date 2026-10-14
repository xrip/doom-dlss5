# Doom / raylib PoC

C23, Clang, CMake, raylib 6.0, OpenGL 3.3. A separate `wad-convert`
tool turns any original `DOOM2.WAD` level (MAP01..MAP32) into a `.drmap`
scene and its original music into a `.mid` file. The viewer reads
only the prepared scene: it has no WAD parser or tessellator.

## Build and run

Need CMake 3.28+, Ninja, Git, and Clang 19+ with C23 `#embed` support.
Windows also needs the Visual Studio C++ Build Tools and Windows SDK.
The first CMake run downloads pinned raylib and libtess2 sources.
Python is optional; it adds the synthetic format checks to CTest.

From PowerShell in this folder, with Clang on PATH:

```powershell
$env:PATH = 'C:/Program Files/LLVM/bin;' + $env:PATH
cmake --preset clang
cmake --build --preset clang
ctest --preset clang
./build/doom-raylib.exe ./build/MAP01.drmap
```

If `DOOM2.WAD` is in the source folder, the build runs the converter.
CMake runs it again only when the WAD or converter changes. No game
assets are in the source code; WADs and prepared scenes are local files.

For a WAD at another path:

```powershell
./build/wad-convert.exe C:/Games/Doom2/DOOM2.WAD ./build/MAP01.drmap
./build/doom-raylib.exe ./build/MAP01.drmap
```

On Linux, with Clang, Ninja, and the GLFW desktop development packages:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/doom-raylib ./build/MAP01.drmap
```

## Controls

WASD walks; mouse turns the view. Walls block motion and allow sliding.
The player goes up steps of at most 24 Doom units and falls to lower floors.
Left Shift runs. E / Space uses doors, floor switches, and lifts. Left click activates
gun-trigger doors. Touch cards or skull keys to unlock doors of that color.
The colored squares show held keys. Tab frees or takes the mouse; R goes back
to the player-one start. Escape closes the window. There is no jump.

N turns debug noclip on or off. In noclip, Space / left Ctrl moves up / down.
Going back to walking resets to the player start. `--noclip` starts in this
mode; `--overview` also uses noclip.

Movement has 35 Hz acceleration/friction, distinct walk/run/strafe speeds,
view bob, step smoothing, and 90-degree horizontal FOV. Walking has no mouse pitch.
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
./build/doom-raylib.exe ./build/MAP01.drmap --inspect
./build/doom-raylib.exe ./build/MAP01.drmap --frames 2 --screenshot ./build/map01-start.png
./build/doom-raylib.exe ./build/MAP01.drmap --overview --frames 2 --screenshot ./build/map01-overview.png
```

Finite frame runs use a hidden window, but still need a working OpenGL
context. `--inspect` does not open a window.

## Scope

The target is the original Doom II IWAD. The scene has original wall
textures, world-aligned floors and ceilings, sector height changes,
texture offsets and pegging, palette light, sky surfaces, and static
sprites for single-player medium difficulty.

The converter builds closed sector contours from linedefs and uses
libtess2 for concave areas, holes, and separate contours. Every closed sector's
triangle area must match its signed outline area. This avoids small
gaps caused by rounded classic BSP splits. BSP is used only in the
converter to find the floor height and light at thing positions. Original
open-outline sectors use original BSP regions instead of guessed closing edges.

The viewer uploads prepared meshes and index textures once. Doors and floors update
affected mesh buffers, including texture pegging and UV motion. Each pixel
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
or sprite angle/frame selection. Sky projection and the choice of light
table row are approximations; this is not a pixel-exact Doom renderer.
Monster-only triggers and boss-death progression need combat/AI and remain
unimplemented. Debug noclip lets you explore past them. This does not claim
full game or demo parity; item pickups and combat remain a separate task.

See [FORMAT.md](docs/FORMAT.md) for the scene format and
[PROVENANCE.md](docs/PROVENANCE.md) for implementation sources.
