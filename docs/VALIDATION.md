# PoC validation

## Player shadow

The Clang build and all 22 non-NGX tests pass (`build/player-shadow-suite.log`).
Generated GPU fixtures check player voxel shadows with transparent sprite
texels, alpha-tested sprite fallback, yaw, horizontal/vertical motion,
noclip hiding, and unchanged raster color/motion with ray lighting off.
CPU checks cover player model import without an extra map actor and shared
sprite floor anchoring below transparent patch rows.

Real MAP01 WAD input renders four frames in each mode with synchronization
validation and `--no-ngx` (`build/player-shadow-voxel.log` and
`build/player-shadow-sprite.log`). Both use 45 ray instances: world, 43 map
objects, and one hidden-from-raster player. The local player model has
11,794 triangles and a foot-relative height of 1.75 world units. NGX was
not run; the body uses its idle pose in both paths.

Walking mouse look now accepts vertical input, with limits of +/-89 degrees.
A local harness calling the real input path checks upward/downward camera
targets, both limits, mouse release, noclip, and unchanged player feet.
The old `build/MAP01.drmap` lacked `PLAYA1`; it was reconverted. Startup
now reports the selected player caster or tells the user to reconvert.
First-person MAP01 courtyard captures at a 70-degree downward pitch show
the voxel player's shadow (`build/player-courtyard-on.png`, versus
`build/player-courtyard-off.png`; 144,544 changed pixels at 1280x800).
The enclosed start-room floor has no direct light at the tested pose, so
its ambient-only image does not change when the caster is hidden. No fake
player-following light or shadow blob was added.

## Voxel performance and WAD cache

### Moving MAP13 regression

The static timings below did not cover walking/animation. A native-only
hidden-window benchmark now moves the camera each frame and ticks sprite
animation once per four frames. The first 16 of 128 frames are warm-up;
work size is 640x480, output 640x360, VSync/validation/NGX are off.

| MAP13 | Mean before | p95 before | Max before | Mean after | p95 after | Max after |
| --- | --- | --- | --- | --- | --- | --- |
| Ray off | 5.722 ms | 12.574 ms | 101.432 ms | 1.352 ms | 1.527 ms | 2.955 ms |
| Ray on | 9.219 ms | 17.735 ms | 113.344 ms | 1.681 ms | 2.071 ms | 3.665 ms |

Shared local pose meshes, GPU current/previous transforms, and static pose
BLASes replace per-actor CPU geometry updates and the combined BLAS refit.
The TLAS selects each actor's current pose/transform. A longer 512-frame
ray-on run gives 1.584 ms mean, 1.792 ms p95, 4.011 ms max. The same run
passes synchronization validation (validation overhead is excluded from
the performance numbers). Repeated pose/hidden cycles have shadow checks.
The fixed MAP01 capture is pixel-identical to the earlier CPU-vertex image.

```powershell
./build/check-renderer.exe ./build/cache/dbde6066a9b83e55/MAP13.drmap --voxels --benchmark 512 --ray-lighting
```

The cache key above is this build's local key. The benchmark is deliberately
native-only; these are not NR/RR/FG results or a guarantee for every camera path.

### Static baseline and cache checks

The final build uses only `build/` for binaries. Native runtime checks use
that full-featured executable with `--no-ngx`, not a second build tree.
Test scene fixtures are under `build/cache/tests/`.
All 22 selected CPU/native-Vulkan checks pass (`build/yagni-final-root.log`);
NGX GPU tests are deliberately excluded.

Static MAP01, RTX 5060 Ti, 640x480 work size, 1280x800 output, 120 real
frames with 16 warm-up frames, no VSync/music/HUD/readback/validation:

| Native path | Before | After |
| --- | --- | --- |
| Voxels + ray lighting, mean real frame | 20.196 ms | 0.963 ms |
| GPU world stage | 10.186 ms | 0.330 ms |

This intermediate change skipped unchanged object transforms/uploads and
used one device-local raster/ray vertex buffer. Shared poses now replace
its per-actor mesh copies, as measured above. An opaque/culling shader
trial had no clear gain and was removed. This static result is not an
interactive animation benchmark or an NR/RR/FG performance claim.

`wad-preparation` uses generated two-map WAD/KVX/MUS fixtures and a copied
viewer/converter in a path with spaces. It checks cache hits, missing
outputs, completion markers, WAD/definition/model changes, map selection,
voxel opt-in/out, and clean errors. It runs `--inspect`, never GPU/NGX.

The real WAD prewarm converted all 32 voxel levels into
`build/cache/dbde6066a9b83e55/`; a second pass reported 32 cache hits and zero
conversions. All 32 cached native sidecars load. `build/doom-raylib.exe
DOOM2.WAD MAP07 --no-ngx --frames 4 --validation --no-music` uses that cache
and renders cleanly with synchronization validation. No updater processes
remain. The cache key is local/build-specific, not a fixed path contract.
All 32 cached voxel levels also render two frames each, sequentially, with
ray lighting, synchronization validation, and `--no-ngx`. The native
all-map CTest path now explicitly uses `--no-ngx` too; `ctest -LE ngx`
includes it and excludes all NGX-labeled tests.

A generated two-map exit fixture also passes the real interactive
use/Enter path from MAP01 to MAP02 with automatic preparation. The same
`--no-ngx` process passes F12 fullscreen/windowed, maximize, minimize,
restore, and close (`build/root-wad-window.out.log` and `.err.log`).

## Optional voxel models

The facing regression uses an asymmetric pivot and checks implicit/default
facing, explicit zero, and 90/180/270 degree offsets. It fails with the old
mirrored raw-Y conversion and passes with `(x-pivotX, pivotZ-z, y-pivotY)`
before yaw. Triangle winding changes with the axis sign and remains outward.
Existing prepared packs must be regenerated; their vertex positions are
already baked. No per-model facing overrides were added.

The earlier Clang C23 build without NGX/FG passed 19 focused CPU/native-Vulkan
checks (`build/voxel-native-checks.log`). New checks cover KVX slab/table
bounds, exposed faces, greedy merging, outward triangle winding, scale,
pivots, palette remapping, yaw, definition precedence, missing models,
sidecar rejection, 35 Hz spin, and whole-animation sprite fallback.

Native ray fixtures verify opaque model shadows despite transparent source
sprite texels, floor anchoring independent of sprite baseline, moved/hidden
models, self-shadowing within one mesh, and shared-pose selection. Existing sprite
and plane-occlusion checks still pass.

All 32 original maps convert with the two local voxel definitions, and all
32 native sidecars load with `--inspect --voxels`. A real MAP01 native ray
capture passes synchronization validation: 21 frame meshes replace all
43 objects (`build/voxel-map01-native.png`). The resulting image was
visually checked. The updater process count remained zero.

Before shared poses, MAP19 reserved about 9.7 million per-actor vertices
and rendered two native ray-lit frames with synchronization validation
(`build/voxel-map19-native.log`). This historical capacity check is not a
performance claim. The current renderer no longer makes those mesh copies.

The NGX-enabled target builds, including voxel-capable RR/NR/SR/FG paths,
but those runtimes were not executed again because of the updater incident.
RR/FG appearance and performance with voxel meshes remain unverified.

## YAGNI review

The full Clang build passes all 28 CTest checks, including every Doom II
map through native, NR+SR, FG, ray+NR+SR+FG, and RR+FG paths
(`build/yagni-final-ctest.log`). The C23 build without NGX/FG passes all
15 CPU checks.

The review found that one overflowing sprite disabled plane occlusion for
all sprites. Wall-only depth is now limited to overflowing sprites; the
others draw against the merged world/sprite depth. A regression checks that
a normal sprite behind a floor stays hidden when an offscreen sprite
overflows its own ceiling (`build/yagni-sprite-regression.log`).

The final focused rerun passed all eight CPU/GPU fixture checks, but its
32-map RR repeat was stopped after NVIDIA's updater accumulated about
3,095 `nvngx_update.exe` processes (47.6 GB summed working sets). The tests
had repeatedly launched NGX; driver updater children outlived the games.
The test tree and updater processes were terminated, and the remaining
updater count was zero. No driver or system registry settings were changed.
Do not repeat NGX batch tests until updater containment is addressed.

## DLSS5 startup mode

Startup again selects the requested NR+SR preset: style 1, two passes,
ray lighting on, and FG off. `--dlss5` explicitly selects NR+SR; `--rr`
or F10 selects the separate RR path. `vulkan-default-mode` checks that
startup executes NR and does not accidentally select RR. GPU runs also
checked the default, `--rr --dlss5`, and `--rr` choices with validation.

## MAP07 Mancubus ceiling clipping

FATTA1 is 73x65 and FATTB1 is 88x70; with floor anchoring their tops reach
129 and 134 Doom units in MAP07 rooms with ceiling 128. Quad sizes were
already current; the world ceiling depth clipped the artwork.
Sprites crossing a sector plane now use alpha-tested wall-only depth,
then merge their visible depth back for NR/RR/FG. The ordinary path remains
unchanged for sprites that fit. WAD offsets and frame scale are unchanged.

The GPU regression compares both frames with no-ceiling references, checks
opaque/transparent walls, and repeats transitions into/out of ceiling overflow.
The old depth rule fails it; the fix passes native, ray-lighting, and RR+FG
checks with synchronization validation. A real MAP07 RR capture also passes
(`build/map07-sprite-fixed.png`). Existing converted maps need no update.

## Ray reconstruction, natural lights, and fullscreen

The scene render size is fixed at 640x480. RR and NR+SR are separate
paths; FG color, HUD-less color, UI color/alpha, and generated output use
the window/display size. RR replaces the separate SR pass. The NR choice,
style, and pass count stay saved while RR is active.

GPU fixtures cover moving/hidden/transparent sprite shadows and colored
sprite light reaching the floor. CPU fixtures cover sector contrast,
sky/closed sectors, animated/hidden/moving lights, palette color, and the
32-source bound. RR checks cover DLAA/quality modes, camera motion, NR
mode isolation after equal warm-up, resize, and FG. The 21 focused checks
pass; all 32 maps also pass the RR+FG synchronization-validation run.
The native-only C23 build passes all 15 CPU checks.

F12 fullscreen/windowed transitions, maximize/minimize/restore, and close
pass with RR, FG, and synchronization validation. `--fullscreen --visible
--frames 4 --fg --validation` also starts at the monitor's 3840x2160 size
and exits cleanly. Logs are in `build/final-rr-fullscreen.*.log`.

Static MAP01, RTX 5060 Ti, 120 real frames, 16 warm-up frames, no VSync,
music, HUD, readback, or validation during the timing runs:

| Output | Mode | Mean real frame | GPU world | GPU AI | GPU SR | GPU HUD/FG |
| --- | --- | --- | --- | --- | --- | --- |
| 1920x1080 | RR | 3.219 ms | 0.155 ms | 1.537 ms | 0.026 ms | 0.024 ms |
| 1920x1080 | NR x2 + SR | 8.467 ms | 0.146 ms | 5.224 ms | 1.006 ms | 0.022 ms |
| 3840x2160 | RR | 6.868 ms | 0.149 ms | 3.269 ms | 0.077 ms | 0.097 ms |
| 3840x2160 | NR x2 + SR | 11.729 ms | 0.168 ms | 5.177 ms | 2.280 ms | 0.104 ms |
| 3840x2160 | RR + FG | 13.923 ms | 0.158 ms | 3.334 ms | 0.078 ms | 5.294 ms |

The 4K FG run counted 120 real and 119 generated presentations. These
counts do not measure monitor scanout. FG has significant cost for this
small scene; it remains off by default. `GPU-AI` is RR or NR according to
the selected path, not both. Timing commands use `--size WIDTH HEIGHT
--frames 120 --profile --no-vsync --no-music`, adding `--rr` for RR,
`--dlss5` for NR+SR, and `--rr --fg` for RR+FG. The 1080p timings precede the final UI recomposition
addition; the listed 4K timings include it.

## Reviewed Vulkan checkpoint

The full Clang build passes all 23 CTest checks; the native-only C23 build
passes all 14. The full suite includes conversion and Vulkan checks across
all 32 Doom II levels. The window test passes maximize, minimize, restore,
and clean close on Windows PowerShell 5 and PowerShell 7 with Vulkan
synchronization validation enabled.

Defaults are NR on, style 1, two passes, FG off, ray lighting on, and SR
performance. A four-frame default capture matches the explicit settings
byte for byte (`build/yagni-default.png`, `build/yagni-explicit.png`).
The cleanup and review scope are recorded in [YAGNI-REVIEW.md](YAGNI-REVIEW.md).

## Sprite animations (DRMAP006)

The Clang build converts MAP01 to 93 textures and 43 sprites. All 110
supported thing types were checked against the pinned WhackEd4 editor data:
49 animated types share 47 spawn loops; 61 types have static spawn images.
Frame order, individual durations, and fullbright bits match the data.

Checks cover exact tick boundaries, unequal frame durations, loop wrap,
hidden/static sprites, scene round trips, and invalid frame data. A GPU
fixture checks that a frame change selects a different texture, returns to
the original image after a full loop, and preserves motion history.
All 32 Doom II maps convert and pass interaction coverage with DRMAP006.
Native and combined NR/SR/FG/ray renderer checks pass Vulkan validation.
All 32 maps also render with NR, SR, FG, and ray lighting together under
Vulkan synchronization validation (`doom2-ray-neural-levels`).

## Earlier checkpoints

Checked on Windows with Clang 23.1.1, CMake, Ninja, and raylib 6.0.

The local Doom II MAP01 converts to 84 textures, 191 mesh batches,
1,572 prepared triangles, and 43 single-player medium-difficulty sprites.
There are 59 collision sectors, 370 lines, and 399 flat regions.
The DRMAP005 `.drmap` file is 2,565,808 bytes with 387 wall bindings. All 59 sector triangle areas
match their original linedef outlines. The converter reads back
each file it writes to check the scene format.

CTest covers:

- Geometry and endian self-tests, including an area with a hole.
- 47 synthetic WAD/scene checks: a room, a hole, separate contours,
  a concave outline, case-insensitive texture names, palette index zero,
  transparent patch gaps, truncated files, bad counts, bad references,
  non-finite coordinates, trailing scene data, collision references,
  a closed door with its region, sector tag, and linedef special,
  sprite baseline correction, sky material assignment, and MIDI sidecar export.
- 19 player movement checks: wall sliding, corners, line endpoints,
  narrow gaps, fast movement, 24/25-unit steps, headroom, falls, landing,
  line flags, and closed/partly open/open door heights.
- Loading and inspecting the prepared local MAP01 without a GPU window.
- 35 Hz walk/run/strafe speeds and friction coming to rest.
- Normal/fast door movement, 150/1050-tic waits, key locks, pickup, repeat
  use, switches, walk/gun triggers, obstruction, vertices, and texture UVs.
- MUS timing, channels, percussion, controllers, velocity, pitch bend,
  header/instrument bounds, score-end events, and malformed data.
- All 32 original levels: conversion, native loading, correct original
  song mapping, and complete MIDI format-0 event validation.
- A MAP01 route at 35 updates per second: drop from the start ledge,
  climb the entrance steps, and stop at the corridor wall. Floor heights
  go from 56 Doom units down to 8 and back to 56.
- 2,000 deterministic MAP01 movement updates, with floor, ceiling, and
  solid-wall radius checks after each update.

GPU checks use hidden OpenGL windows, two frames each. The player start
and map overview were rendered and their PNG output was inspected.
The start view shows the expected entrance room, floor steps, textures,
and two standing enemy sprites. Screenshots are local build outputs:
`build/map01-start.png` and `build/map01-overview.png`.
The collision build also renders `build/map01-collision-start.png`.
New inspected GPU output: `build/map01-feel-start.png`, `build/sky-outdoor.png`,
`build/door-closed.png`, and `build/door-open.png`. The outdoor view was
checked after closing the black horizon gap with a full sky background.

All MAP01..MAP32 scenes were rendered locally in hidden OpenGL windows.
Files are ready in `build/levels`. WinMM started the actual MAP01 MIDI and
the viewer rendered 120 frames before stopping/closing the sequencer.
This checks the API path; audio output was not recorded.

The `convert-all.ps1` wrapper was run against the local IWAD and produced
all 64 scene/MIDI outputs. `run-map.ps1` passed PowerShell syntax validation;
its viewer is the same binary exercised by the per-level GPU checks.
All fourteen CTest tests pass. Door/key, floor/lift, sector-mover, and shared
interaction tests also passed AddressSanitizer.

Floor/lift checks cover highest/lowest targets, normal/fast speed, one-shot
and repeat switches, walk triggers, 105-tic waits, blocked rise reversal,
grounded/airborne riders, sprite floor/ceiling anchors, and busy sectors.
The real MAP01 checks use switch 102 to lower all four tagged barriers and
switch 62 to lower and return the lift. Player movement and gravity remain
active during the lift ride. Lower wall vertices follow the barriers.
The native reader rejects invalid sprite owners and ceiling anchors.
All 32 maps were reconverted to DRMAP005 and rendered again. Inspected
floor/lift GPU snapshots are `build/floor-check-barriers-after.png` and
`build/floor-check-lift-down.png`; before snapshots are in the same folder.

The mover test has 397 checks for rising floors, opposite-direction targets,
texture/type transfer timing, platforms, pause/resume, ordered stairs,
blocked floors, and normal/fast/silent crushers. The all-level test now
checks all 88 original linedef action types and all 13 original sector
effect types against explicit handling, then runs 500 sector ticks per map.
Lookup coverage is not a claim that every gameplay route was walked.

MAP01 interaction tests cover all twelve action lines and all five secrets,
including an airborne player crossing the grate's west edge with movement
collision active. Lowering the small lift and northern barriers leaves the
separate imp/plasma secret doors shut. The small lift's closed secret door
keeps complete upper/lower wall coverage. Double activation was not
reproduced; a screenshot/position is needed to diagnose that report further.
The rocket alcove's opened GPU view was inspected at
`build/levels/interaction-check-grate-after.png`.

A live Windows test started a prepared MAP01 scene at its exit switch and
sent E/Enter messages to that process's own GLFW window. It reached a new
MAP02 window and started the MAP02 MIDI sidecar. The log is
`build/interaction-route.log`. This checks exit handling, GPU resource
cleanup/reload, and music switching; it does not record the sound output.

See [YAGNI-REVIEW.md](YAGNI-REVIEW.md) for the full file review and cleanup checks.

These checks establish a working viewer with world collision. They do not establish
pixel-exact Doom lighting, sky projection, or gameplay behavior.

## Native Vulkan migration (2026-10-14)

Clang 23.1.1, Vulkan SDK 1.4.357.0, RTX 5060 Ti, driver 617.42:

- The native build passes all 16 CTest checks with `DOOM_GPU_TESTS=ON`.
  This includes rendering all 32 converted maps with synchronization
  validation. The ordinary non-GPU suite remains 14 checks.
- GPU motion readback checks static frames, camera motion, history reset,
  moving MAP01 barriers/lift, HUD isolation, and resize. A sky-only scene
  checks zero translation motion, rotation motion, and history reset.
- MAP01 reference images from the previous renderer were compared at
  1280x800. Spawn differs at 3 pixels, overview at 42, and the grate view
  at 2094; the largest mean absolute RGB channel error is 0.027 on a
  0..255 scale. These are visual migration checks, not Doom pixel parity.
- `--motion` produced a valid float PFM with finite, exactly zero values
  for a repeated static view. Captures are under `build/vulkan-*.png`.
- A native-only build rejects `--dlss5` with exit code 1. NR builds now use
  the public NGX SDK and the separately verified signed 310.8 runtime.
  The earlier beta-header and assigned-application-ID gates were removed
  after checking working community integrations.
- The C++ NGX bridge has been compiled and run on the GPU above. The full
  18-test suite passed, including actual NR evaluation on all 32 maps.
  The normal launcher and a visible interactive run from `build` were
  checked. The runtime filename workaround is handled by the launcher.

Build and test logs: `build/vulkan-build.log`, `build/vulkan-tests.log`.
See [VULKAN.md](VULKAN.md) for the SDK/runtime evidence and setup.

### Presentation and billboard regressions

The initial migration checks compared offscreen images and missed a second
sRGB encode during presentation. A new dark two-color sprite fixture first
failed the expected palette-color check; after correcting the surface
format it failed the left/right orientation check. Correcting the sign of
the sprite's Z rotation made both checks pass.

Color readback now uses the same blit operation and destination format as
presentation, then converts BGRA byte order to RGBA for screenshots/tests.
The fixture checks exact palette values and sprite size/orientation around
an eight-angle camera orbit. The full 16-test suite passes with these checks
and synchronization validation. Logs of the failing and passing runs are
`build/vulkan-regression-before.log`, `build/vulkan-regression-color-fixed.log`,
and `build/vulkan-regression-fixed.log`.

### Super Resolution integration

The SR path uses a separate NGX feature, input-resolution color/depth/motion,
display-resolution output, and a display-resolution HUD. The 310.9.1 SR
runtime initially produced two synchronization validation errors for SDK
image clears. An NGX Vulkan callback now supplies the missing write-to-clear
dependency; validation remains enabled. Shutdown ordering also releases
the SDK's resources before shutting down the direct NR snippet.

`build/sr-callback.log` records 32 frames with NR and SR evaluation and exit
code zero. `build/sr-regression.log` records all 18 tests passing, including
rendering all 32 Doom II maps with NR and default performance SR.
`build/sr-mode-tests.log` records the expanded GPU test passing quality,
balanced, and performance modes, with full-size output, resize while NR is
off, and NR resuming after resize. These tests enable synchronization
validation; they do not prove temporal image quality during gameplay.

Isolated MAP01 timing on RTX 5060 Ti, 3840x2160, one NR pass, 240 frames
(16 warm-up frames excluded), validation on, no captures or other game
instance running:

| SR mode | Render size | Frame time | GPU NR | GPU SR stage |
| --- | --- | --- | --- | --- |
| Off | 3840x2160 | 34.482 ms | 30.917 ms | 0.085 ms (copy) |
| Performance | 1920x1080 | 16.679 ms | 8.596 ms | 3.965 ms |
| Quality | 2560x1440 | 19.801 ms | 13.719 ms | 2.833 ms |

All runs exited zero with synchronization validation enabled. Performance
mode reaches the current presentation limit near 60 FPS; these are rendered
frames, not generated frames. Logs: `build/sr-4k-clean-off.log`,
`build/sr-4k-clean-performance.log`, and `build/sr-4k-clean-quality.log`.
Runs with PNG captures or a second game instance are not comparable to
these measurements.

`build/sr-temporal-tests.log` adds a 32-frame camera translation followed by
a camera cut back to the same MAP01 spawn view and 32 settling frames.
Mean absolute RGBA replay differences (0..255 units) were 0.000 for quality,
0.000 for balanced, and 0.116 for performance; all are below the test's
1.0-unit reset threshold. Static views over another 32-frame jitter cycle
differed by 0.623, 1.420, and 0.274 respectively. This checks history reset
and quantifies static changes; it is not a full moving-image quality test.

A separate `DOOM_DLSS5=OFF` build in `build/native-check` also passed all
14 non-GPU tests and a 32-frame MAP01 Vulkan validation smoke run.
Logs: `build/native-check-build.log`, `build/native-check-tests.log`,
and `build/native-check-smoke.log`.

### Frame generation bring-up

This section records the earlier Streamline trial, now replaced by direct
NGX evaluation and an application-owned presentation worker.

The official Streamline 2.14.1 x64 release package is staged locally under
`build/_deps/streamline-sdk-2.14.1`. Authenticode checks passed for
`sl.interposer.dll`, `sl.common.dll`, `sl.reflex.dll`, `sl.dlss_g.dll`, and
`nvngx_dlssg.dll`. Windows reports hardware scheduling enabled (`HwSchMode=2`).

The local `build/fg_probe.cpp` smoke probe links the Streamline interposer
instead of the Vulkan loader. With the project's custom GUID it reports
successful Streamline init, Vulkan 1.3 instance and device creation, FG and
Reflex support on RTX 5060 Ti, and shutdown. `build/fg-probe-device.log`
records the result. The probe uses a minimal local runtime folder, but the
NGX driver cache is also active, so this is not proof of an offline install.

This proves API/device compatibility only. It does not generate frames or
prove coexistence with direct NGX SR/NR. Engine integration, Reflex timing,
HUD-less inputs, resize handling, and actual presented-frame counts still
need validation before claiming FG works.

The experimental engine build is under `build/fg-check`, with
`DOOM_FRAME_GENERATION=ON` and the local Streamline package. The default
build still has FG disabled. The engine now routes GLFW surface creation
through the interposer, supplies depth/motion/HUD-less inputs and Reflex
markers, and queries FG state per presented application frame. Direct NGX
NR/SR releases its features first but leaves final NGX core shutdown to
Streamline; shutting the shared core down twice caused a crash.

FG-only and combined NR/SR/FG finite runs reach rendering and clean exit
without validation. This is not validation-clean: SDK-owned fake-swapchain
images, clear/copy/fill commands, and pacer semaphores still produce errors.
Logs: `build/fg-input-fixes.log`, `build/fg-shared-owner.log`. These errors
remain visible; FG is not yet a supported release mode.

### Window state regression

`tests/check_window.ps1` starts its own engine process and sends real
`WM_SYSCOMMAND` maximize, restore, minimize, and restore commands twice,
then sends `WM_CLOSE` and requires exit code zero. It checks actual window
state, not only process survival, and does not target another game window.

```powershell
./tests/check_window.ps1 -Exe ./build/doom-raylib.exe -Scene ./build/MAP01.drmap `
    -Modes @('--dlss5', '--validation') -Log ./build/window-sr
./tests/check_window.ps1 -Exe ./build/fg-check/doom-raylib.exe -Scene ./build/fg-check/MAP01.drmap `
    -Log ./build/window-fg
```

NR+SR passed with synchronization validation enabled
(`build/maximize-sr-validation-test.log`). FG+NR+SR passed without validation
(`build/maximize-fg-fixed-test.log`); this does not waive the FG validation
faults above. The resize fix ends unsubmitted FG tokens before rebuilding
the swapchain, disables FG during recreation, and starts fresh history
afterward. Finite tests can use `--visible` to exercise a real window.

The same FG+NR+SR test also passed ten full cycles (40 window-state
commands), followed by clean close: `build/window-fg-stress-test.log`.
Use `-Cycles 10` to repeat this stress check. Validation was off for this
run; it proves survival of these window changes, not absence of SDK faults.

### FG runtime update comparison

Matching 60-frame NR+SR+FG runs were tested with synchronization validation
and either local Streamline 2.14.1 plugins or NVIDIA's signed NGX cached
plugins (version directory `134672`, including PCL 2.14.16). Both rendered
60 frames and reported 120 presentations, then exited with validation
failure: 37 errors for local plugins, 39 for cached plugins. Counts include
duplicate-message limits and thread timing differences, so the two-error
difference is not a quality or stability score. Both retain the same core
fake-image layout, copy, clear, and pacer semaphore faults.

The cached-plugin experiment was reverted; the bridge uses the specified
local package again. Logs: `build/fg-local-validation.log` and
`build/fg-ota-validation.log`. Similar errors remain open in NVIDIA's
[issue 84](https://github.com/NVIDIA-RTX/Streamline/issues/84) and
[issue 112](https://github.com/NVIDIA-RTX/Streamline/issues/112).

A separate visible 4K performance run with NR, performance SR, and 2x FG
rendered 240 application frames and reported 480 presentations (SDK status
zero). After 16 warm-up frames, mean application-frame time was 18.279 ms;
GPU NR took 9.510 ms and SR 4.313 ms. This is about 54.7 rendered FPS.
The SDK presentation count is not an independent measurement of monitor
scanout or frame pacing. Validation was off, and the known SDK faults still
apply. Log: `build/fg-4k-performance.log`.

### Direct NGX FG feasibility

An isolated headless probe under `build/fg-direct-probe` links native Vulkan
and the public NGX SDK, without Streamline. The first run created the FG
feature but reported ten buffer-device-address feature-enable errors.
After querying and enabling that required Vulkan feature, creation passed
with zero validation errors (`build/fg-direct-probe/create-bda.log`).

The SDK requested `VK_KHR_get_physical_device_properties2`,
`VK_NVX_binary_import`, `VK_NVX_image_view_handle`,
`VK_KHR_buffer_device_address`, and `VK_KHR_push_descriptor`.
All were available on the RTX 5060 Ti. This proves feature creation only;
evaluation, generated pixels, pacing, and integration remain unverified.
The engine has not switched FG backends.

Before a backend change, the current default build again passed all 18
tests (`build/pre-direct-regression.log`). The all-map neural test now
requires both the NR and SR evaluation markers, not only the NR marker.
That stronger check passed separately for all 32 maps with Vulkan
validation (`build/all-map-nr-sr-explicit.log`).

### Direct FG engine validation

The engine now uses direct NGX FG and a native C23 presentation worker,
not Streamline. The probe's transfer dependencies and scoped image-layout
initialization were applied through NGX's Vulkan callbacks. All four probe
hashes stayed identical while errors went from 27 to zero
(`build/fg-direct-probe/evaluate-layout-fixed.log`).

The first engine run found a host-access race between acquire and present.
Both swapchain APIs now share a lock; acquisition is nonblocking under that
lock and waits outside it, so the presentation worker can release images.
A 64-frame run then passed synchronization validation with 64 real and 63
generated presentations (`build/direct-fg-serialized.log`). Reset and
skipped frames are not counted as generated.

All 20 tests passed in `build/direct-fg-full-tests.log`, including FG on all
32 maps with real/generated presentation-count checks. The GPU regression
also covers camera movement, map movers, sky, mode changes, resize, and all
three SR modes. Generated-image readback differs from real moving frames
by mean RGBA values 1.319, 1.373, and 0.963 for quality, balanced, and
performance SR (`build/direct-fg-readback-test.log`). This confirms non-copy
game output, not just SDK success or an FPS multiplier.

Two maximize/minimize/restore cycles and clean close pass with NR+SR+FG
and synchronization validation enabled (`build/window-direct-fg-test.log`).

A separate visible 4K run rendered 240 frames and successfully presented
240 real plus 239 generated frames. Mean application frame time after
16 warm-up frames was 17.278 ms (about 57.9 rendered FPS). GPU NR was
8.115 ms, SR 3.920 ms, and the combined HUD/FG stage 3.798 ms.
Validation was off for this timing run; no captures or other GPU tests ran
at the same time. These are presentation-call counts, not measured monitor
scanout. Log: `build/direct-fg-4k.log`.

The FG DLL from the pinned public SDK is byte-identical to the earlier
probe runtime and has SHA-256
`ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82`.

### Ray-query lighting

The RTX 5060 Ti reports ray query, acceleration structure, shader int64,
runtime descriptor array, and nonuniform sampled-image support. Its scratch
address alignment is 128 bytes; the renderer queries it rather than assuming
an alignment. The optional lighting stage builds a world BLAS/TLAS and
updates it when world vertex positions change.

The GPU fixture in `vulkan-ray-lighting` proves more than an image change:
an opaque wall gives a floor sample of 122; moving that wall away raises it
to 158; restoring it returns 122; a fully transparent wall yields 158.
Disabling ray lighting returns the exact classic palette value 128.
This tests dynamic occlusion, alpha rejection, and the classic fallback.
Log: `build/ray-shadow-test.log`.

The same checks pass with NR, SR, and FG, alongside camera, mover, sky,
resize, and generated-pixel checks (`build/ray-neural-test.log`). All
32 maps also pass the combined ray+NR+SR+FG validation run with generated
presentation counts (`build/ray-all-levels.log`).

A local native FIFO run was limited near 252 ms per frame both with and
without lighting. That is not the ray shader's GPU cost. With explicit
`--no-vsync`, the separate 1280x800 ray run measured 0.730 ms application
frames and 0.470 ms for GPU world rendering, before the static-AS refit
optimization. Logs: `build/ray-native-control.log`,
`build/ray-visible-profile.log`, `build/ray-uncapped-profile.log`.

The completed build passes all 23 tests, including the full lighting and
neural pipeline on all 32 maps (`build/ray-full-tests.log`). The native-only
build also passes its 14 tests (`build/ray-native-tests.log`). Neither build
requires Streamline. Maximize/minimize/restore and clean close pass with
ray lighting, NR, SR, FG, and synchronization validation enabled
(`build/window-ray-full-test.log`).

The final native captures `build/ray-classic.png` and `build/ray-final.png`
were visually inspected at the same camera and resolution. The direct
lighting changes shading while retaining the original texture content.
This check is separate from NR's model-driven image changes.

The final 4K run with ray lighting, NR, performance SR, and FG rendered
240 application frames and presented 240 real plus 239 generated frames.
After warm-up, application-frame time was 17.977 ms (about 55.6 rendered
FPS); GPU world/lighting was 0.794 ms, NR 8.152 ms, SR 3.887 ms, and
HUD/FG 3.795 ms. Validation was off for this isolated timing run.
Log: `build/ray-full-4k.log`.

The normal `build/doom-raylib.exe` was rebuilt with NR/SR/FG support. Its
final 64-frame ray+NR+SR+FG smoke run passed synchronization validation,
started WinMM MIDI playback, and presented 64 real plus 63 generated
frames (`build/ray-default-music-smoke.log`). A separate native-only build
also rendered ray lighting with validation and no NGX dependency
(`build/ray-native-only-smoke.log`). Final build and smoke logs contain no
compiler warnings or validation diagnostics.
