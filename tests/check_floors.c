#include "specials.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(bool result, const char *message) {
    if (!result) fail("Floor check failed: %s", message);
}

static void ticks(Scene *scene, Specials *specials, Player *player, unsigned count) {
    for (unsigned tick = 0; tick < count; ++tick) specials_tick(scene, specials, player);
}

static void synthetic(void) {
    SceneSector sectors[] = {{.floor = 0, .ceiling = 8}, {.floor = 4, .ceiling = 8, .tag = 7},
                             {.floor = 2, .ceiling = 8}};
    CollisionLine lines[] = {
        {.start = {3, 0}, .end = {3, 8}, .front = 0, .back = UINT32_MAX, .special = 102, .tag = 7},
        {.start = {4, 0}, .end = {4, 8}, .front = 0, .back = 1},
        {.start = {8, 0}, .end = {8, 8}, .front = 1, .back = 2}};
    Sprite sprites[] = {{.sector = 1, .position = {6, 4, 4}},
                        {.sector = 1, .position = {6, 8, 4}, .ceiling = true}};
    Scene scene = {.sectors = sectors, .sector_count = 3, .lines = lines, .line_count = 3,
                   .sprites = sprites, .sprite_count = 2};
    Player user = {.feet = {2, 0, 4}, .sector = 0};
    Player rider = {.feet = {6, 4, 4}, .sector = 1};
    Specials specials = specials_create(&scene);
    expect(!specials_use(&scene, &specials, &(Player){.feet = {4, 0, 4}}, 3.14159265f), "back side rejects use");
    expect(specials_use(&scene, &specials, &user, 0), "lower switch starts");
    expect(lines[0].special == 0 && specials.switch_ticks[0] == UINT32_MAX, "one-shot switch stays changed");
    ticks(&scene, &specials, &rider, 1);
    expect(sectors[1].floor == 4 - 1.0f / 32 && rider.feet.y == sectors[1].floor, "one unit per tic and rider follows");
    ticks(&scene, &specials, &rider, 63);
    expect(sectors[1].floor == 2 && specials.movers.floors[1].state == FLOOR_STOPPED, "highest neighbor target");
    expect(sprites[0].position.y == 2 && sprites[1].position.y == 8, "ground sprites move, hanging sprites stay");
    lines[0].special = 62; sectors[1].floor = 4; rider.feet.y = 4;
    expect(specials_use(&scene, &specials, &user, 0), "repeat lift starts");
    expect(!specials_use(&scene, &specials, &user, 0) && lines[0].special == 62, "busy lift preserves repeat switch");
    ticks(&scene, &specials, &rider, 32);
    expect(sectors[1].floor == 0 && rider.feet.y == 0 && specials.movers.floors[1].wait == 105, "lift lowers four units per tic");
    ticks(&scene, &specials, &rider, 104);
    expect(specials.movers.floors[1].state == FLOOR_WAITING && sectors[1].floor == 0, "wait lasts 105 tics");
    ticks(&scene, &specials, &rider, 1);
    expect(specials.movers.floors[1].state == FLOOR_RISING && specials.switch_ticks[0] == 0, "wait expires and switch resets");
    ticks(&scene, &specials, &rider, 32);
    expect(sectors[1].floor == 4 && rider.feet.y == 4 && specials.movers.floors[1].state == FLOOR_STOPPED, "lift returns with rider");
    lines[0].special = 123;
    expect(specials_use(&scene, &specials, &user, 0), "fast lift starts");
    rider.feet.y = 5;
    ticks(&scene, &specials, &rider, 1);
    expect(sectors[1].floor == 3.75f && rider.feet.y == 5, "fast lift eight units, airborne player not carried");
    ticks(&scene, &specials, NULL, 15 + 105);
    sectors[1].ceiling = PLAYER_HEIGHT;
    rider.feet.y = 0;
    ticks(&scene, &specials, &rider, 1);
    expect(sectors[1].floor == 0 && specials.movers.floors[1].state == FLOOR_LOWERING, "blocked rise reverses without crushing");
    specials_free(&specials); specials = specials_create(&scene);
    sectors[1].ceiling = 8; sectors[1].floor = 4; lines[0].special = 19; lines[0].back = 0;
    specials_cross(&scene, &specials, user.feet, (Position){3.5f, 0, 4}, NULL);
    expect(lines[0].special == 0 && specials.movers.floors[1].state == FLOOR_LOWERING, "walk-once floor trigger");
    lines[0].special = 23;
    expect(!specials_use(&scene, &specials, &user, 0) && lines[0].special == 23, "busy sector preserves one-shot");
    specials_free(&specials); specials = specials_create(&scene);
    specials.doors[1].state = DOOR_CLOSING;
    expect(!specials_use(&scene, &specials, &user, 0), "door excludes floor movement in same sector");
    specials.doors[1].state = DOOR_STOPPED; lines[0].tag = 0;
    expect(!specials_use(&scene, &specials, &user, 0), "tag zero does not affect untagged sectors");
    specials_free(&specials);
}

static Player switch_player(const Scene *scene, const CollisionLine *line, float *yaw) {
    float dx = line->end.x - line->start.x, dz = line->end.z - line->start.z;
    float length = hypotf(dx, dz), nx = -dz / length, nz = dx / length;
    *yaw = atan2f(nz, -nx);
    return (Player){.feet = {(line->start.x + line->end.x) * 0.5f + nx, scene->sectors[line->front].floor,
                             (line->start.z + line->end.z) * 0.5f + nz}, .sector = line->front};
}

static void snapshot(Scene *scene, const char *prefix, const char *suffix, Player player, float yaw) {
    if (!prefix) return;
    scene->spawn = player.feet; scene->spawn.y += PLAYER_EYE; scene->yaw = yaw;
    char *path = scene_sidecar(prefix, suffix);
    scene_write(scene, path); free(path);
}

static void real_map(const char *path, const char *prefix) {
    Scene scene = scene_read(path);
    Specials specials = specials_create(&scene);
    float yaw;
    Player user = switch_player(&scene, &scene.lines[84], &yaw);
    expect(scene.lines[84].special == 102 && scene.lines[84].tag == 3, "MAP01 barrier switch metadata");
    snapshot(&scene, prefix, "-barriers-before.drmap", user, yaw);
    expect(specials_use(&scene, &specials, &user, yaw), "MAP01 barrier switch use ray");
    unsigned moving = 0;
    for (size_t sector = 0; sector < scene.sector_count; ++sector)
        moving += specials.movers.floors[sector].state == FLOOR_LOWERING;
    expect(moving == 4, "all four tagged barriers start together");
    ticks(&scene, &specials, NULL, 64);
    const unsigned barriers[] = {46, 47, 50, 51};
    for (size_t index = 0; index < sizeof(barriers) / sizeof(barriers[0]); ++index)
        expect(scene.sectors[barriers[index]].floor == 48.0f / 32, "MAP01 barriers lower 112 to 48");
    for (size_t index = 0; index < scene.wall_count; ++index) {
        const WallSurface *wall = &scene.walls[index];
        if (wall->kind != WALL_LOWER) continue;
        const CollisionLine *line = &scene.lines[wall->line];
        if (line->front != 46 && line->back != 46) continue;
        SceneVertex vertices[6];
        wall_vertices(&scene, wall, vertices);
        for (size_t vertex = 0; vertex < 6; ++vertex)
            expect(vertices[vertex].y == 48.0f / 32, "lower wall geometry follows barrier floor");
    }
    snapshot(&scene, prefix, "-barriers-after.drmap", user, yaw);
    user = switch_player(&scene, &scene.lines[126], &yaw);
    expect(scene.lines[126].special == 62 && scene.lines[126].tag == 5, "MAP01 lift switch metadata");
    snapshot(&scene, prefix, "-lift-before.drmap", user, yaw);
    expect(specials_use(&scene, &specials, &user, yaw), "MAP01 lift switch use ray");
    Player rider = {.sector = 21, .view_height = PLAYER_EYE};
    float largest = 0;
    for (size_t index = 0; index < scene.region_count; ++index) {
        const CollisionTriangle *region = &scene.regions[index];
        if (region->sector != 21) continue;
        const MapPoint *points = region->points;
        float area = fabsf((points[1].x - points[0].x) * (points[2].z - points[0].z) -
                           (points[1].z - points[0].z) * (points[2].x - points[0].x));
        if (area <= largest) continue;
        largest = area;
        rider.feet = (Position){(points[0].x + points[1].x + points[2].x) / 3, scene.sectors[21].floor,
                                (points[0].z + points[1].z + points[2].z) / 3};
    }
    expect(largest > 0, "MAP01 lift collision footprint");
    for (unsigned tick = 0; tick < 28; ++tick) {
        specials_tick(&scene, &specials, &rider);
        player_tick(&scene, &rider, 0, 0, 0, false);
        expect(rider.feet.y == scene.sectors[21].floor, "MAP01 player rides lift downward with gravity active");
    }
    expect(scene.sectors[21].floor == 40.0f / 32 && specials.movers.floors[21].wait == 105, "MAP01 lift lowers 152 to 40");
    snapshot(&scene, prefix, "-lift-down.drmap", user, yaw);
    for (unsigned tick = 0; tick < 105 + 28; ++tick) {
        specials_tick(&scene, &specials, &rider);
        player_tick(&scene, &rider, 0, 0, 0, false);
        expect(rider.feet.y == scene.sectors[21].floor, "MAP01 player rides returning lift with collision active");
    }
    expect(scene.sectors[21].floor == 152.0f / 32 && scene.lines[126].special == 62, "MAP01 lift returns and remains usable");
    expect(specials_use(&scene, &specials, &user, yaw), "MAP01 lift can be used again");
    specials_free(&specials); scene_free(&scene);
}

int main(int argc, char **argv) {
    if (argc > 3) fail("Usage: check-floors [MAP01.drmap [snapshot-prefix]]");
    synthetic();
    if (argc >= 2) real_map(argv[1], argc == 3 ? argv[2] : NULL);
    puts("Passed floor targets, speed, timers, triggers, riding, sprites, and obstruction checks");
    return 0;
}
