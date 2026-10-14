#include "doors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(bool condition, const char *name) {
    if (!condition) fail("Door test failed: %s", name);
}

static void synthetic(void) {
    SceneSector sectors[] = {{0, 4, 0, 0}, {0, 0, 0, 7}, {0, 5, 0, 0}};
    CollisionLine lines[] = {{{4, 0}, {4, 8}, 0, 1, 4, 1, 0},
                             {{4.5f, 8}, {4.5f, 0}, 2, 1, 4, 1, 0},
                             {{3, 0}, {3, 8}, 0, UINT32_MAX, 1, 0, 0}};
    Scene scene = {.sector_count = 3, .line_count = 2, .sectors = sectors, .lines = lines};
    Player player = {.feet = {2, 0, 4}, .sector = 0};
    Doors doors = doors_create(&scene);
    expect(!doors_use(&scene, &doors, &player, 3.14159265f), "use faces the door");
    expect(doors_use(&scene, &doors, &player, 0), "64-unit use reach");
    for (unsigned tick = 0; tick < 61; ++tick) doors_tick(&scene, &doors, &player);
    expect(sectors[1].ceiling == 122.0f / 32 && doors.sectors[1].state == DOOR_OPENING, "two units per tic");
    doors_tick(&scene, &doors, &player);
    expect(sectors[1].ceiling == 124.0f / 32 && doors.sectors[1].state == DOOR_WAIT_OPEN, "four-unit ceiling gap");
    for (unsigned tick = 0; tick < 149; ++tick) doors_tick(&scene, &doors, &player);
    expect(doors.sectors[1].state == DOOR_WAIT_OPEN && doors.sectors[1].wait == 1, "150-tic wait");
    doors_tick(&scene, &doors, &player);
    expect(doors.sectors[1].state == DOOR_CLOSING, "wait then close");
    for (unsigned tick = 0; tick < 62; ++tick) doors_tick(&scene, &doors, &player);
    expect(sectors[1].ceiling == 0 && doors.sectors[1].state == DOOR_STOPPED, "full close cycle");
    expect(doors_use(&scene, &doors, &player, 0), "repeat use");
    doors_tick(&scene, &doors, &player);
    expect(doors_use(&scene, &doors, &player, 0) && doors.sectors[1].state == DOOR_CLOSING, "use reverses opening");
    expect(doors_use(&scene, &doors, &player, 0) && doors.sectors[1].state == DOOR_OPENING, "use reverses closing");
    for (unsigned tick = 0; tick < 212; ++tick) doors_tick(&scene, &doors, &player);
    player.feet.x = 4.25f; player.sector = 1;
    for (unsigned tick = 0; tick < 35; ++tick) doors_tick(&scene, &doors, &player);
    expect(doors.sectors[1].state == DOOR_OPENING && sectors[1].ceiling >= PLAYER_HEIGHT, "closing reverses on player");
    doors_free(&doors); doors = doors_create(&scene);
    sectors[1].ceiling = 0; lines[0].special = 31;
    player.feet.x = 2; player.sector = 0;
    expect(doors_use(&scene, &doors, &player, 0) && lines[0].special == 0, "D1 open once");
    for (unsigned tick = 0; tick < 500; ++tick) doors_tick(&scene, &doors, &player);
    expect(sectors[1].ceiling == 124.0f / 32 && doors.sectors[1].state == DOOR_OPEN, "D1 stays open");
    doors_free(&doors); scene.line_count = 3; doors = doors_create(&scene);
    sectors[1].ceiling = 0; lines[0].special = 1;
    expect(!doors_use(&scene, &doors, &player, 0), "wall blocks use ray");
    lines[2].special = 103; lines[2].tag = 7;
    expect(doors_use(&scene, &doors, &player, 0) && lines[2].special == 0, "tagged switch opens door");
    doors_free(&doors); doors = doors_create(&scene);
    sectors[1].ceiling = 0; lines[2].special = 2; lines[2].back = 0;
    doors_cross(&scene, &doors, player.feet, (Position){3.5f, 0, 4});
    expect(doors.sectors[1].state == DOOR_OPENING && lines[2].special == 0, "walk-over door trigger");
    doors_free(&doors);
    doors = doors_create(&scene); scene.line_count = 2; sectors[1].ceiling = 0;
    lines[0].special = 26; player.keys = 0;
    expect(!doors_use(&scene, &doors, &player, 0), "blue door requires a key");
    player.keys = 4;
    expect(!doors_use(&scene, &doors, &player, 0), "red key does not open blue door");
    player.keys = 1;
    expect(doors_use(&scene, &doors, &player, 0), "blue key opens blue door");
    doors_free(&doors); doors = doors_create(&scene); sectors[1].ceiling = 0;
    lines[0].special = 117;
    expect(doors_use(&scene, &doors, &player, 0), "fast manual door");
    doors_tick(&scene, &doors, &player);
    expect(sectors[1].ceiling == 8.0f / 32, "fast door eight units per tic");
    doors_free(&doors); scene.line_count = 3; doors = doors_create(&scene); sectors[1].ceiling = 4;
    lines[2].special = 16; lines[2].back = 0;
    doors_cross(&scene, &doors, player.feet, (Position){3.5f, 0, 4});
    for (unsigned tick = 0; tick < 64; ++tick) doors_tick(&scene, &doors, &player);
    expect(sectors[1].ceiling == 0 && doors.sectors[1].state == DOOR_WAIT_CLOSED, "close then wait thirty seconds");
    for (unsigned tick = 0; tick < 1050; ++tick) doors_tick(&scene, &doors, &player);
    expect(doors.sectors[1].state == DOOR_OPENING, "thirty-second timer expires at 1050 tics");
    doors_free(&doors); doors = doors_create(&scene); sectors[1].ceiling = 0;
    lines[2].special = 46;
    expect(!doors_use(&scene, &doors, &player, 0), "gun door does not use ordinary use key");
    expect(doors_shoot(&scene, &doors, &player, 0), "shoot trigger opens door");
    doors_free(&doors);
    Asset assets[3] = {{.name = "BKEYA0"}, {.name = "YSKUA0"}, {.name = "RKEYA0"}};
    Sprite sprites[3] = {{.asset = 0, .position = {2, 0, 4}}, {.asset = 1, .position = {2, 0, 4}},
                         {.asset = 2, .position = {7, 0, 4}}};
    scene.assets = assets; scene.sprites = sprites; scene.sprite_count = 3; player.keys = 0;
    player_pickup_keys(&scene, &player);
    expect(player.keys == 3 && sprites[0].hidden && sprites[1].hidden && !sprites[2].hidden,
           "cards and skull keys are picked up by color");
    puts("Passed door speed, wait, use, switch, trigger, reversal, and obstruction checks");
}

static void real_map(const char *path, const char *prefix) {
    Scene scene = scene_read(path);
    Doors doors = doors_create(&scene);
    CollisionLine *line = &scene.lines[170];
    float dx = line->end.x - line->start.x, dz = line->end.z - line->start.z;
    float length = hypotf(dx, dz), nx = -dz / length, nz = dx / length;
    Player player = {.feet = {(line->start.x + line->end.x) * 0.5f + nx,
                             scene.sectors[line->front].floor,
                             (line->start.z + line->end.z) * 0.5f + nz}, .sector = line->front};
    scene.spawn = player.feet; scene.spawn.y += PLAYER_EYE;
    scene.yaw = atan2f(nz, -nx);
    uint32_t sector = line->back;
    expect(sector == 29 && line->special == 1, "original MAP01 manual door");
    if (prefix) {
        char *closed = scene_sidecar(prefix, "-closed.drmap"); scene_write(&scene, closed); free(closed);
    }
    expect(doors_use(&scene, &doors, &player, scene.yaw), "MAP01 door use");
    float before = scene.sectors[sector].ceiling;
    WallSurface *surface = NULL;
    for (size_t index = 0; index < scene.wall_count; ++index)
        if (scene.walls[index].line == 170 && scene.walls[index].kind == WALL_UPPER && !scene.walls[index].side) surface = &scene.walls[index];
    expect(surface != NULL, "MAP01 door render binding");
    SceneVertex old_vertices[6], new_vertices[6]; wall_vertices(&scene, surface, old_vertices);
    for (unsigned tick = 0; tick < 100; ++tick) doors_tick(&scene, &doors, &player);
    wall_vertices(&scene, surface, new_vertices);
    expect(scene.sectors[sector].ceiling > before && new_vertices[0].y > old_vertices[0].y,
           "MAP01 door collision and render height move together");
    expect(new_vertices[2].v != old_vertices[2].v, "door texture moves with ceiling");
    if (prefix) {
        char *opened = scene_sidecar(prefix, "-open.drmap"); scene_write(&scene, opened); free(opened);
    }
    player_move(&scene, &player, -nx * 2, -nz * 2, 0.1f);
    expect(player.sector != line->front, "MAP01 open doorway passes player");
    printf("MAP01 door 29: ceiling %.3f -> %.3f; wall and UV bindings checked\n", before, scene.sectors[sector].ceiling);
    doors_free(&doors); scene_free(&scene);
}

int main(int argc, char **argv) {
    if (argc == 1) synthetic();
    else if (argc == 2 || argc == 3) real_map(argv[1], argc == 3 ? argv[2] : NULL);
    else fail("Usage: check-doors [MAP01.drmap [snapshot-prefix]]");
    return 0;
}
