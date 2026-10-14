# Map interaction

Scope: original Doom II MAP01..MAP32, medium difficulty, one player.
Combat, non-key item pickups, monster movement, and actor collisions are
outside this map-interaction pass. The runtime uses only prepared scenes.

## Action inventory

The local IWAD contains 88 distinct linedef action types. Every type has an
explicit handler or classification. The all-level test checks each imported
linedef against this inventory; it does not merely check that a map loads.
Classic door actions are listed in [MOVEMENT.md](MOVEMENT.md).

| Family | Added actions used by the IWAD |
| --- | --- |
| Raise floors: ceiling, next higher floor, fixed amount, shortest lower texture | 5, 18, 30, 59, 91, 94, 119, 131, 140 |
| Lower with numeric texture/type transfer | 37 |
| Raise/change platforms | 15, 20, 22, 47, 67, 68 |
| Perpetual platforms / stop | 87, 89 |
| Build 8-unit / fast 16-unit stairs | 7, 8, 100, 127 |
| Crushers / stop / fast / silent | 49, 73, 74, 77, 141 |
| Light to 255 / 35 | 13, 35, 138, 139 |
| Player teleport, once / repeat | 39, 97 |
| Monster-only teleport, classified but no monster simulation | 125, 126 |
| Scroll first-sidedef wall left | 48 |
| Normal use / walk exit | 11, 52 |
| Secret use / walk exit | 51, 124 |

All actions retain their original trigger kind, tag targets, once/repeat
rule, and key constraints. A use ray selects one nearest action within 64
Doom units; it does not pass through one switch to activate another door.
Shoot actions use a longer ray but do not create a weapon or combat system.
Walk triggers require a side change and a segment intersection, including
crossing from an exact point on the line. Touching a line on the same side
does not activate it. Teleports additionally require front-to-back crossing.

Stairs follow front-to-back sector links with matching floor textures.
Change platforms use the trigger's first sector as the texture/type model;
numeric lower/change actions use the first adjoining sector at the target
height. Texture changes update owner flat batches, not shared wall textures.
Perpetual platforms and crushers retain their state when paused and resume
through their start trigger. Moving sprites remain tied to their own planes.

## Sector effects

The IWAD uses sector types 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13, 16, and 17.
They have explicit behavior: random flashes, fast/slow strobes, glow, fire
flicker, secret discovery, damaging floors, and delayed door closure.
Type 14 delayed opening and type 11 damaging exits are also handled.
Live sector light reaches wall vertices, flats, and non-fullbright sprites.
Fullbright sprite flags are preserved by the converter.

Standing on a secret sector's own floor counts it once. The HUD shows the
found/total count and a brief message. Hazard types 7/5/4,16 take 5/10/20
health every 32 map ticks while the player is grounded. Health starts at
100; R restores it at the start. No armor, suits, or healing pickups are
simulated in this pass.

Teleport markers are original type-14 things. The lowest-numbered tagged
destination sector and its first marker are used. The player lands on that
sector's current floor, takes the marker's angle, loses momentum, and has
an 18-tic movement delay. Sprites are not blocking actors, so telefrag
damage and monster-only teleports are not simulated.
Some original teleport lines have no tagged destination (for example,
MAP12 line 311, tag 13). They remain no-ops; the converter does not invent
sectors or marker links to repair original map content.

## MAP01 links

MAP01 has twelve interactive linedefs and five secret sectors. Its secrets
are separate links, not a general “land on any raised floor” event:

- Line 126 / action 62 lowers and returns lift sector 21 only. The secret
  wall behind it is a separate manual door, line 124 / sector 22.
- The hidden switch, line 3 / action 103, opens sector 31 toward the courtyard.
- Crossing the west edge of the raised grate, line 242 / action 2, opens
  sector 58, the rocket-launcher alcove. Other grate edges have no action.
- Line 84 / action 102 lowers sectors 46, 47, 50, and 51. It does not open
  the separate north secret door, line 47 / sector 55, or switch door 42.
- Line 75 / action 103 opens sector 42. Manual lines 166/167 operate exit
  door 37; lines 170/173 operate side secret door 29.
- Line 162 / action 11 completes the level. The secret sector IDs are
  8, 17, 28, 41, and 57.

These links are verified from the original WAD records and real-scene
tests. Lowering either the lift or the four barriers did not reproduce
double door activation; tests also check the small lift's closed door wall
coverage. A screenshot/position is still needed for the reported visual bug.

## Exit and limits

An exit changes its switch and stops the map simulation. The completion
overlay gives the secret count. Enter loads the next `.drmap` and `.mid`
from the same folder; missing scenes give a clear message. Use
`tools/convert-all.ps1` to prepare the whole sequence. MAP15 and MAP31 secret
exits lead to MAP31 and MAP32; normal MAP31/32 exits lead to MAP16. MAP30's
normal exit ends the sequence. Maps start fresh; there is no saved campaign
inventory or original intermission artwork.

This is a clean-room floating-point simulation, not demo/fixed-point parity.
Random light sequences and perpetual platform starting direction are not
the original PRNG. Crushers stop at a player body and apply damage rather
than compressing it as original actor clipping does. Static monsters do not
block movers. Boss-death progression, sound effects, animated texture/sprite
frames, inventory, and combat remain separate work. No generalized/Boom,
Hexen-format, or arbitrary PWAD compatibility is promised.

Behavior tables come from the public
[Boom reference](https://raw.githubusercontent.com/fabiangreffrath/woof/master/docs/boomref.txt),
not Doom engine or port source code.
