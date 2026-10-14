# Doom II maps

These tools prepare and view original Doom II maps MAP01 through MAP32. You
need a legally obtained original `DOOM2.WAD`; this project does not include
or download game assets. The WAD is read as input and is never changed.

## Build

Use PowerShell from the project folder. Install Clang 19 or newer, CMake
3.28 or newer, Ninja, Git, and the Visual Studio C++ Build Tools with the
Windows SDK. Put Clang on `PATH`, then run:

```powershell
$env:PATH = 'C:/Program Files/LLVM/bin;' + $env:PATH
cmake --preset clang
cmake --build --preset clang
ctest --preset clang
```

The preset puts the executables in `build`. If a script reports a missing
executable, run these commands; the scripts do not install tools for you.
The first CMake configure may fetch the pinned raylib and libtess2 source
dependencies; it does not fetch a game WAD.

## Convert and run one map

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
- `Left Shift` runs. `E` or `Space` uses normal, fast, keyed, and switch doors; walk-over doors activate on crossing.
- Left click activates gun-trigger doors. It does not fire a weapon or start combat.
- Keys and skulls are picked up by proximity. `R` returns to the player start and keeps collected keys.
- `Tab` releases or captures the mouse. `Esc` closes the viewer.
- `N` toggles noclip. In noclip, `Space` and `Left Ctrl` move up and down.

The original Doom II MAP01–MAP32 convert locally. The viewer and door logic
remain a partial game simulation: door states are not saved, monsters stay
static, and elevators, floor triggers, teleports, and exit transitions are
not implemented. Use `N` noclip to explore routes past these missing game
systems. There is no enemy AI, weapon combat, or full game progression.
Success on the original maps does not promise exact compatibility with every
Doom II WAD or gameplay behavior.

## Validation

The latest integration run converted all 32 maps, validated their MIDI
sidecars, rendered a GPU frame, and passed all nine CTest tests. The batch
script stops at the first conversion error and does not run map-specific test
executables.
