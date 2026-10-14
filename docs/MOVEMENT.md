# Player movement and door data

The normal camera is a walking player. Its feet have an x/z position and
a height. The camera stays 41 Doom units above the feet. The collision
body has radius 16 and height 56. It can step up by 24 units, but not 25.
WASD motion is horizontal. There is no jump; mouse pitch is available only
in debug noclip. Walking has a 90-degree horizontal field of view.

The controller sweeps a circle against blocking line segments and their
endpoints, then projects the remaining motion along contact surfaces.
Motion is split into short steps so portal height checks change as the
player moves across sectors. A floor query uses the converter's exact
triangles and keeps the current sector on shared edges. No GPU queries,
WAD reads, or memory allocations are needed during movement.

One-sided lines and impassable lines block the player. Two-sided lines
use the higher adjoining floor and the lower adjoining ceiling. An
opening must fit the full player height and the upward step limit.
Nearby passable lines contribute to the floor support and ceiling limit
over the player's radius. Combined clearance is checked before a move
is accepted. Drops use gravity and land at the supporting floor.

Movement uses 35 Hz ticks. Forward commands are 25/50 walking/running;
strafe commands are 24/40. Acceleration is command strength divided by
32 Doom units per tick; friction keeps 29/32 of momentum. Terminal forward
speeds are about 291.67/583.33 units per second, and running strafe is 466.67.
Diagonal motion is not normalized. The eye has movement bob and step
smoothing. These replace the old instant 7/20-world-unit camera speeds.
Circle sliding, floating-point integration, and view smoothing are this
project's implementation; exact fixed-point/demo behavior is not claimed.

The dimensions and step rule come from
[The Unofficial Doom Specs, chapter 4](https://github.com/nukeop/TheUnofficialDoomSpecs/blob/master/Chapter4.md).
The circle sweep and gravity integration are this project's controller,
not code from a Doom engine or port.
The movement data and speed formula are documented in the
[movement comparison](https://github.com/0dot0repeating/quakemovement#useful-info-i-guess).

## Doors

Sector floor and ceiling heights are the runtime source for collision.
Lines keep both sector IDs, flags, action specials, and tags. Closed
sectors keep their footprint and floor/ceiling mesh bindings. A door is a sector with a
moving ceiling; it is not stored as a permanent solid collision wall.

Tests change a door ceiling from closed to partly open to open. The same
lines then block or pass the player without a collision rebuild. The
converter also has a synthetic closed-door fixture that checks the
sector footprint and activation metadata survive the scene round trip.

E / Space uses doors and door switches within 64 units. Left click activates
gun-trigger doors; weapon combat is not simulated. Normal doors move 2 units
per tic, fast doors 8. The opening stops 4 units below the lowest adjacent
ceiling. Raise/wait/close doors wait 150 tics; close/wait/open actions wait
1050 tics (30 seconds). Closing raise doors reverse on the player; close-only
doors pause. Repeated switches display SW2 for 35 tics, then return to SW1.

Supported classic door specials:

- Manual: 1, 26..28, 31..34, 117, 118.
- Use switches: 29, 42, 61, 63, 99, 103, 111..116, 133..137.
- Walk triggers: 2..4, 16, 75, 76, 86, 90, 105..110.
- Gun trigger: 46.

Actions preserve key colors, once/repeat behavior, and tags. Doortrack and
door wall UVs follow current sector heights. Only changed GPU buffers are
updated. The action definitions and key colors are in the
[Doom Builder guide](https://www.doombuilder.com/files/doombuilder1guide.pdf).

## Keys and exploration

Touch cards or skulls to collect their blue/yellow/red key color. Cards and
skulls of the same color open the same classic lock. Colored squares show
held keys. `--all-keys` grants all colors for exploration; R and returning
from noclip keep them. Picked-up key sprites disappear.

Lifts, floor movers, teleports, boss triggers, exits/level transitions,
combat, door sounds, and non-key pickups remain unimplemented. N provides
noclip for exploration past those features.
