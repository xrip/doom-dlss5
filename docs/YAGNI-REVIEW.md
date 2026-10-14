# YAGNI, KISS, and DRY review

Scope: every project-owned source, shader, test, build file, utility, and
document. All 33 files present at the start were read. New shared helpers
and this report were also reviewed. Generated build files, downloaded
dependencies, Git internals, and the user's commercial WAD are not project
source and are excluded from the commit. There were no earlier commits.

## Changes

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
