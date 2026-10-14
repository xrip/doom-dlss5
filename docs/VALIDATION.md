# PoC validation

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
