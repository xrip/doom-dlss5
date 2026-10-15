# Doom II maps

Optional KVX models: add `-Voxels` to the conversion/run scripts.
See [VOXELS.md](VOXELS.md) for sidecars, fallback, and limits.

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
ctest --preset clang -LE ngx
```

Do not run NGX GPU batch checks while the known updater-process issue is
unresolved. Use `--no-ngx` for native runs with the same `build/` binary.
The `-LE ngx` filter excludes NGX tests. Native all-map tests explicitly
pass `--no-ngx`. `--inspect` and
`--prepare-all` do not start GPU or NGX even in the DLSS-enabled build.

The preset puts the executables in `build`. If a script reports a missing
executable, run these commands; the scripts do not install tools for you.
The first CMake configure may fetch the pinned GLFW, stb, and libtess2 source
dependencies; it does not fetch a game WAD.

## Convert and run one map

The viewer can now prepare a WAD itself:

```powershell
./build/doom-raylib.exe ./DOOM2.WAD MAP07
./build/doom-raylib.exe ./DOOM2.WAD --prepare-all --voxels
```

Automatic output goes only to `cache/<key>/` beside the viewer, never into
the WAD folder. The key covers WAD/converter bytes, voxel definition bytes,
and KVX names, sizes, and modification times. A completion marker is written
only after conversion succeeds; missing required outputs trigger conversion
again. Cached levels are reused on launch and on Enter after a level exit.
`--prepare-all` fills all 32 entries without a window, GPU, or NGX startup.

Keep `wad-convert` beside the viewer. Voxel sources are searched beside the
viewer and in its two parent folders; WAD mode enables them when all source
paths exist. `--voxels` requires them; `--no-voxels` disables them. Direct
`.drmap` runs still use sprites unless `--voxels` is given.

Each cache entry has the `.drmap`, original `.mid` when present, optional
`.drvox`, and a `.ready` marker. The cache is disposable: close the game
before deleting it. There is no automatic eviction. KVX edits that preserve
both file size and modification time require removing the old cache.
These are local file-cache checks, not a security/content-authentication scheme.
Inputs are fingerprinted at startup; restart after changing source assets.

The explicit PowerShell/converter workflows below remain available when
you want to choose an output folder rather than use the automatic cache.

DLSS-enabled builds start in NR+SR mode. F10 selects the separate RR path;
F6 toggles NR, F7 changes style, and F8 changes pass count in that path.
SR stays active when NR is toggled off. The scene is always 640x480;
the window size is the upscale target. Direct runs accept `--no-rr` for
NR+SR and `--sr off|quality|balanced|performance` for the DLSS model hint.
See [VULKAN.md](VULKAN.md) for the required SDK/runtime setup.
With a frame-generation-enabled build, `-FrameGeneration` adds `--fg` and
also enables `--dlss5`. It preserves the same map conversion and MIDI path.
`-RayLighting` adds ray-traced direct lighting; it can be combined with
`-FrameGeneration`. Press `L` in the viewer to compare with classic lighting.
Use `-Fullscreen` or the engine's `--fullscreen` flag to start fullscreen;
F12 switches back and forth while preserving the window position and size.

`-Wad` is required. Relative WAD and output paths start at the project
folder. The explicit scripts default to `build/cache/manual`.

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
New-Item -ItemType Directory -Force ./build/cache/manual | Out-Null
.\build\wad-convert.exe '.\DOOM2.WAD' '.\build\cache\manual\MAP02.drmap' MAP02
```

## Controls and scope

- `W`, `A`, `S`, `D` walk; the mouse looks left/right and up/down (up to 89 degrees).
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
