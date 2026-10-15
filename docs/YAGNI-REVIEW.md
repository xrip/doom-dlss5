# YAGNI, KISS, and DRY review

## Player shadow and mouse look checkpoint

- Reused shared pose meshes, object matrices, and the ray TLAS for one
  player caster. Raster draws still include only map sprites; there is no
  extra render pass, light, actor framework, or format version.
- Imported only the needed `PLAYA1` idle pose. Missing models use the
  existing alpha-tested sprite path; missing patches report the need to
  reconvert old prepared maps rather than adding runtime WAD parsing.
- Shared the visible-bottom-row floor offset between map conversion and
  the player sprite. Feet and yaw drive the caster; view bob and pitch do not.
- Enabled walking pitch in the existing input path with a fixed 89-degree
  limit. Ground movement and use/shoot direction are unchanged.
- Kept tests for voxel/sprite shadows, cutouts, facing, movement, hiding,
  and exclusion from raster color/motion. Local input and real-map capture
  harnesses remain ignored build artifacts, not new engine layers.

The Clang build and 22 non-NGX tests pass. Tests do not initialize NGX or
change driver settings. Source WAD/voxel assets stay outside the commit.

## Optional voxel checkpoint

Reviewed the voxel converter, native sidecar, renderer ranges, shadow
materials, animation state, tools, tests, and docs, including the existing
sprite and ray paths they extend. The earlier pending lighting/RR review
below remains part of this checkpoint.

- Kept `DRMAP006` unchanged. Prepared-file input needs an explicit voxel
  option; WAD input can detect the local voxel sources. KVX and definition
  parsing stay out of the game.
- Reused the existing palette, animation tick, object vertex buffer, ray
  queries, motion vectors, and material outputs. No actor framework,
  backend layer, or asset-management framework was added.
- Removed duplicate per-vertex instance history. Object motion uses current
  and previous matrices; shared local pose vertices stay immutable.
- Palette validation and 6-bit RGB expansion run once per source color,
  not once for every candidate WAD palette entry.
- Model rays use an opaque flag, without sampling the sprite cutout.
  TLAS instance IDs identify objects; no primitive/2 assumption or extra
  per-vertex object word remains. Models retain self-shadows and emitter exclusion.
- Fixed mirrored facing in the raw voxel Y axis rather than adding model
  exceptions or changing valid definition defaults. Tests cover the default,
  explicit zero, all cardinal offsets, and outward triangle winding.
- Kept only measured speed changes. Moving MAP13 exposed >100 ms spikes
  hidden by the static test, so shared pose meshes and GPU instance matrices
  replace CPU vertex rewrites. Static pose BLASes are built once; only the
  small TLAS and moving world geometry update. The earlier CPU dirty-range
  cache and an opaque pipeline trial are removed, not layered underneath.
- WAD startup calls the existing separate converter; it adds no WAD parser
  to the viewer. A local fingerprint directory and completion marker give
  cache reuse and recovery without a database or cache service.
- Kept bounded native/KVX input checks and whole-animation sprite fallback.
  Unsupported actor pitch/roll/momentum flags add no stored runtime state.

Native-only checks are used while the NGX updater issue is unresolved.
The same full-featured `build/` executable supports `--no-ngx`; no separate
native build tree is required. CMake's generated scene fixtures also live
under `build/cache/`.
No driver settings, asset packs, or source voxel definitions are changed.

Final safe checks: the Clang build and all 22 selected CPU/native-Vulkan
tests pass. All 32 voxel cache entries prepare, load, and give cache hits
on a second prewarm; all 32 also render with ray lighting and synchronization
validation through the same `build/` binary with `--no-ngx`.
A generated two-map WAD passes real exit/use/Enter
transition, F12, maximize/minimize/restore, and close with `--no-ngx`.
NGX GPU checks have an explicit `ngx` label and remain excluded with
`ctest -LE ngx` because of the unresolved updater issue.

## Ray lighting and reconstruction checkpoint

Reviewed the changes since `eaf5c12` and the existing renderer paths they
extend: natural lights, sprite shadows, RR, NR/SR/FG inputs, sprite plane
occlusion, fullscreen controls, tests, build setup, tools, and docs.

- Removed the separate render-size query: the scene always uses 640x480.
- Shared quality-hint mapping and palette color conversion instead of
  adding separate versions for each DLSS path.
- Light ranking reads power from the light itself, with no duplicate
  power argument or output value. Sector geometry uses its area directly,
  without a second validity flag or setup pass.
- RR and NR+SR remain separate paths. RR does not add a second upscale
  pass or run the experimental NR model on its output. Its initialization
  check no longer depends on SR's separate parameters.
- Sector geometry and sprite emitter colors are built once. Current sector
  brightness, ceiling height, sprite frame, and visibility stay live.
- The sprite depth fix uses the existing quads and only adds draw passes
  when a sprite crosses its sector floor or ceiling. Tests cover animation
  frame sizes, opaque and transparent walls, and switching between paths.
- Limited wall-only depth to overflowing sprites. Other sprites keep plane
  occlusion even when an overflowing sprite is elsewhere in the scene.
  The depth passes share one attachment barrier helper.
- Kept SDK-required buffers, image barriers, and the separate FG UI layer.
  These have present callers and validation tests, not future-only uses.

Defaults remain NR+SR, style 1, two passes, RT on, FG off. RR is selected
with F10 or `--rr`; F12 changes fullscreen without a new window framework.
No WAD, SDK binaries, generated files, or local voxel assets are included.

## Historical Vulkan and sprite checkpoint

Reviewed the pending Vulkan/NGX renderer, presentation worker, shaders,
CMake setup, live controls, HUD, sprite conversion/playback, tests, tools,
and documentation. Also reviewed the preceding map-interaction commit
`27a104e` for repeated logic and unnecessary state.

- Removed the unused renderer validation flag and stored sprite shade.
  Sprite light comes from the live owner sector; DRMAP006 stores no duplicate shade.
- Removed an unused FG device argument and create values ignored by the SDK helper.
- FG-off frames skip the HUDless copy. Swapchain mode comes from the live
  FG state and the explicit no-VSync choice, rather than a stale cached flag.
- Shared the existing trigger enum between doors and sector movers and
  reused the existing line-side calculation. No new action framework.
- Shared the two identical NR runtime-copy steps in CMake.
- Kept Vulkan barriers, FG presentation pacing, and NGX callback workarounds:
  they serve validated resource lifetimes and SDK behavior. The compiled
  fragment shader contains `OpDemoteToHelperInvocation`, so its required
  device feature remains enabled.
- Kept sprite animation as a small converter table plus the existing 35 Hz
  clock. No actor framework, combat states, or WAD reads in the renderer.
- Fixed the window test's PowerShell 5 exit-code handling and cleanup of
  its own failed test process tree.

Defaults match the requested screenshot: NR enabled, style 1, two passes,
FG disabled, and ray lighting enabled. SR stays in performance mode.
Existing key controls and command-line overrides remain available.

Final checks: the Clang build and all 23 CTest checks pass, including all
32 maps through the GPU variants. A fresh C-only build without NGX/FG
passes all 14 checks. Maximize/minimize/restore/close passes with Vulkan
synchronization validation on PowerShell 5 and 7. Default and explicitly
selected screenshot settings produce byte-identical PNG captures.

An existing behavior edge needs a separate focused check: player-sector
contact uses the radius across neighboring linedefs without checking the
blocking flag. It can affect mover/door obstruction near a solid shared
wall. This review does not change that collision rule.

The next lighting task covers AI ray tracing, sprite
shadows, and light sources from bright sectors and glowing sprites.
Those features are outside this checkpoint.

## Historical initial review

This is the historical review for the initial checkpoint. Later floor/lift
support shares controls in `src/specials.c` and `src/sector_movers.c` and
uses DRMAP005. This report's older paths and format checks remain historical.

Scope: every project-owned source, shader, test, build file, utility, and
document. All 33 files present at the start were read. New shared helpers
and this report were also reviewed. Generated build files, downloaded
dependencies, Git internals, and the user's commercial WAD are not project
source and are excluded from the commit. There were no earlier commits.

## Changes

### Current interaction review

The follow-up review checked the current project-owned text files and the
initial committed checkpoint. Requested interaction families are retained:
their need comes from actual original Doom II map actions, not a future
engine roadmap. No renderer backend framework, ECS, or plugin layer is added.

Confirmed waste removed from the later interaction code:

- Remove `silent` flags: sound effects do not exist, so no runtime reads them.
- Remove stored `blocked` flags: tests now observe motion, height, and health
  instead of requiring write-only production state.
- Remove the copied mover sector count; scene data owns its dimensions.
- Share line adjacency and player-radius sector contact checks between doors,
  floors, and crushers. No duplicated collision predicate remains there.

The current build passes all 14 CTest tests, including 397 observable mover
checks and the 32-map action inventory. Native DRMAP005 layout is unchanged
by this cleanup. File validation remains: WAD/native inputs can be malformed.
Small independent test helpers are not replaced by a new test framework.
This is a code review, not a claim of minimum possible code or Doom parity.

### Initial checkpoint

- Share path resolution, tool checks, conversion, and output checks between
  the two PowerShell entry points. Their command-line options stay the same.
- Share segment-distance math between movement and door obstruction checks.
- Share triangle emission between tessellation and the necessary BSP fallback.
  Triangle code no longer pretends to handle arbitrary polygons.
- Share complete-buffer file writes between both music conversion tools.
- Remove the standalone MUS build guard, unused raylib build option, explicit
  default Git-fetch setting, dead sky-wall branch, redundant door flag,
  unreachable MUS default branch, and unnecessary includes.
- Name current door states, triggers, modes, surface planes, and wall kinds.
  Do not change their numeric meanings or the DRMAP003 format.
- Use one player-reset path and one keyboard-input read per rendered frame.
  Use the scene's map name in the window title.
- Replace the transient worker handoff with normal music documentation.
  Ignore prepared scenes and MIDI/MUS assets outside the build directory too.

## File decisions

| File | Decision |
| --- | --- |
| `.gitignore` | Keep build and game-derived data out of commits. |
| `CMakeLists.txt` | Keep pinned dependencies and focused targets; remove unused configuration and duplicate math-library links. |
| `CMakePresets.json` | Keep the single requested Clang/Ninja build. |
| `README.md` | Keep setup, controls, scope, and links to detailed documents. |
| `src/scene.h` | Keep the shared format model; add names for existing surface values and shared helpers. |
| `src/scene.c` | Keep explicit endian I/O and real input checks; share music file writes. |
| `src/geometry.c` | Keep door geometry/UV calculation; share distance math and remove the unused sky-wall condition. |
| `src/convert.c` | Keep real WAD decoding, current assets, tessellation, and required open-outline BSP fallback; share triangle/sprite emission. |
| `src/main.c` | Keep window, input, GPU ownership, debug views, and buffer updates; remove duplicate reset/input paths. |
| `src/movement.h` | Keep the small player API and active movement constants. |
| `src/movement.c` | Keep swept contacts, floors, 35 Hz motion, bob, and key pickup; use shared distance math. |
| `src/doors.h` | Keep current door state; replace numbers with names and remove derived `stay` storage. |
| `src/doors.c` | Keep the existing classic action table and state machine; remove fake player construction and numeric sentinel logic. |
| `src/mus.h` | Keep one converter API, independent of the renderer. |
| `src/mus.c` | Keep bounded event conversion, byte buffering, timing, and malformed-input handling; simplify delay storage and the exhaustive switch. |
| `src/mus_convert.c` | Keep the requested separate CLI; remove the compile guard and duplicate output handling. |
| `src/music.h` | Keep only start, update, and stop. |
| `src/music.c` | Keep WinMM sequencing and the existing non-Windows stubs; no new audio abstraction. |
| `src/shaders/world.vs` | Keep the attributes and transforms consumed by the fragment shader. |
| `src/shaders/world.fs` | Keep original-index color lookup, coverage, and current sky projection. |
| `tests/check_doors.c` | Keep timing, keys, triggers, obstruction, and MAP01 UV checks; use named states. |
| `tests/check_movement.c` | Keep independent geometry checks, speed tests, and deterministic MAP01 walks. |
| `tests/check_music.c` | Keep byte-level expected MIDI events and invalid-score checks. |
| `tests/check_wad.py` | Keep synthetic assets and invalid-format fixtures; no commercial data in tests. |
| `tests/check_levels.py` | Keep all 32 original-map/song checks with an independent MIDI parser. |
| `tools/run-map.ps1` | Keep the convert-and-run entry point and its current options. |
| `tools/convert-all.ps1` | Keep the 32-map loop and stop-on-error behavior. |
| `tools/level-tools.ps1` | New small helper used by both existing entry points; no utility framework. |
| `docs/FORMAT.md` | Keep the actual DRMAP003 layout; correct sky-group wording. |
| `docs/LEVELS.md` | Keep the requested standalone level guide and explicit limits. |
| `docs/MOVEMENT.md` | Keep implemented motion/door rules and supported actions. |
| `docs/MUSIC.md` | Keep current converter/playback details instead of worker status. |
| `docs/MUSIC-WORKER.md` | Remove the obsolete handoff. |
| `docs/PROVENANCE.md` | Keep sources and dependency provenance. |
| `docs/VALIDATION.md` | Keep measured results; link this review. |
| `docs/YAGNI-REVIEW.md` | Record the review scope, decisions, and evidence. |

DRMAP003's existing imported metadata is retained for the documented file
contract. The classic door aliases are data in the existing action table,
not a new simulation subsystem. Bounds checks remain because files can be
invalid. Small independent C test failure helpers are retained rather than
adding a test framework. No ECS, generic resource manager, plugin system,
job system, renderer backend, or unimplemented gameplay system is added.

## Checks

- Clang/CMake build and all nine CTest tests pass.
- All 32 native scenes and MIDI files match the pre-review outputs byte for byte.
- All 32 levels pass GPU smoke checks; the outdoor sky is rendered again.
- MAP32 MIDI starts through WinMM; no sound recording is claimed.
- Door/key and real MAP01 door checks pass AddressSanitizer.
- Shared PowerShell helpers convert and inspect MAP02; all three scripts parse.
- Documentation's local links and Git whitespace checks pass.

This is a scope/code review, not certification of full Doom gameplay parity.
One separate behavior limit remains: debug noclip moves the camera but keeps
the last grounded player position used for door obstruction. A player left
in a doorway can therefore keep that door open while the free camera moves
away. It was not changed by this cleanup. Other gameplay limits are listed
in the level and movement guides.
