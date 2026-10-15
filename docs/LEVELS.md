# Doom II maps

These tools prepare and view original Doom II maps MAP01 through MAP32. You
need a legally obtained original `DOOM2.WAD`; this project does not include
or download game assets. The WAD is read as input and is never changed.

## Build

Use PowerShell from the project folder. Install Clang 19 or newer, CMake
3.28 or newer, Ninja, Git, and the Visual Studio C++ Build Tools with the
Windows SDK, plus the Vulkan SDK with `glslc`. Put Clang on `PATH`, then run:

```powershell
$env:PATH = 'C:/Program Files/LLVM/bin;' + $env:PATH
cmake --preset clang
cmake --build --preset clang
ctest --preset clang
```

The preset puts the executables in `build`. If a script reports a missing
executable, run these commands; the scripts do not install tools for you.
The first CMake configure may fetch the pinned GLFW, stb, and libtess2 source
dependencies; it does not fetch a game WAD.

## Convert and run one map

For a build configured with NR support, add `-Dlss5` to `run-map.ps1`.
F6 toggles neural rendering, F7 changes style, and F8 changes pass count.
SR stays active when NR is toggled off. Direct engine runs accept
`--sr off|quality|balanced|performance`; performance is the NR launch default.
See [VULKAN.md](VULKAN.md) for the required SDK/runtime setup.
With a frame-generation-enabled build, `-FrameGeneration` adds `--fg` and
also enables `--dlss5`. It preserves the same map conversion and MIDI path.
`-RayLighting` adds ray-traced direct lighting; it can be combined with
`-FrameGeneration`. Press `L` in the viewer to compare with classic lighting.

`-Wad` is required. Relative WAD and output paths start at the project
folder. The default output folder is `build/maps`.

```powershell
.\tools\run-map.ps1 -Wad 'C:\Games\Doom II\DOOM2.WAD' -Map MAP02
.\tools\run-map.ps1 -Wad '.\DOOM2.WAD' -Map MAP01 -Output '.\build\local-maps' -NoMusic
.\tools\run-map.ps1 -Wad '.\DOOM2.WAD' -Map MAP03 -AllKeys
```

The script runs `build/wad-convert.exe`, writes `MAPxx.drmap` and its
`MAPxx.mid` music sidecar, then starts the interactive viewer with that
scene. Windows MIDI playback uses WinMM. `-NoMusic` passes `--no-music` to
the viewer. Without it, the viewer tries the scene's `.mid` sidecar.
`-AllKeys` passes the viewer's `--all-keys` exploration cheat.

## Convert every map

```powershell
.\tools\convert-all.ps1 -Wad 'C:\Games\Doom II\DOOM2.WAD'
.\tools\convert-all.ps1 -Wad '.\DOOM2.WAD' -Output '.\build\all-maps'
```

This writes `MAP01.drmap` through `MAP32.drmap` and a matching `.mid` for
each map. It stops on the first converter error. Files from earlier maps
remain in the output folder so you can inspect the progress.

To convert a standalone MUS file, use the other converter:

```powershell
.\build\mus-convert.exe '.\music\D_RUNNIN.mus' '.\build\D_RUNNIN.mid'
```

Or call the WAD converter directly for one scene and sidecar:

```powershell
.\build\wad-convert.exe '.\DOOM2.WAD' '.\build\maps\MAP02.drmap' MAP02
```

## Controls and scope

- `W`, `A`, `S`, `D` walk; the mouse turns the view.
- `Left Shift` runs. `E` or `Space` uses doors, lowering floor switches, and returning lifts; walk triggers activate on crossing.
- Left click activates gun-trigger doors. It does not fire a weapon or start combat.
- Keys and skulls are picked up by proximity. `R` returns to the player start and keeps collected keys.
- `Tab` releases or captures the mouse. `Esc` closes the viewer.
- `N` toggles noclip. In noclip, `Space` and `Left Ctrl` move up and down.

The original Doom II MAP01–MAP32 convert locally. Map geometry interaction,
lights, hazards, secrets, teleports, and exits are active; see
[INTERACTION.md](INTERACTION.md). The simulation is still partial: sector
states are not saved and monsters stay static. Monster-only and boss-death
actions need AI/combat. Use `N` noclip to explore those remaining paths.
There is no weapon combat or full inventory/progression simulation.
Exit switches show map completion. Enter loads the next prepared scene and
its MIDI from the current scene's folder. Run `convert-all.ps1` first so the
next map exists. Missing next scenes produce a message, not a WAD read.
Secret exits use MAP15→MAP31→MAP32; normal exits from MAP31/32 return to MAP16.

Success on the original maps does not promise exact compatibility with every
Doom II WAD or gameplay behavior.

## Validation

The latest integration run converted all 32 maps, validated their MIDI
sidecars, rendered a GPU frame, and passed the CTest suite. Floor tests
check MAP01's four barriers and returning lift, plus speed, delays, riding,
sprite anchors, and blocked lift reversal. The batch
script stops at the first conversion error and does not run map-specific test
executables.

Prepared scenes now use DRMAP006 for sprite animation frames, sector light/material data, sprite
anchors/fullbright flags, and teleport destinations. Reconvert older files
with `convert-all.ps1` or `run-map.ps1`; previous versions are not accepted.
