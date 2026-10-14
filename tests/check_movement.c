#include "movement.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void expect(bool condition, const char *name) {
    if (!condition) fail("Movement test failed: %s", name);
}

static Scene room(SceneSector *sectors, CollisionLine *lines, CollisionTriangle *regions) {
    sectors[0] = (SceneSector){0, 4, 0, 0};
    sectors[1] = (SceneSector){0, 4, 0, 7};
    CollisionLine outline[] = {
        {{0, 0}, {0, 8}, 0, UINT32_MAX, 0, 0, 0},
        {{0, 0}, {4, 0}, 0, UINT32_MAX, 0, 0, 0},
        {{0, 8}, {4, 8}, 0, UINT32_MAX, 0, 0, 0},
        {{4, 0}, {8, 0}, 1, UINT32_MAX, 0, 0, 0},
        {{4, 8}, {8, 8}, 1, UINT32_MAX, 0, 0, 0},
        {{8, 0}, {8, 8}, 1, UINT32_MAX, 0, 0, 0},
        {{4, 0}, {4, 8}, 0, 1, 0, 1, 7}
    };
    CollisionTriangle triangles[] = {
        {{{0, 0}, {4, 0}, {4, 8}}, 0}, {{{0, 0}, {4, 8}, {0, 8}}, 0},
        {{{4, 0}, {8, 0}, {8, 8}}, 1}, {{{4, 0}, {8, 8}, {4, 8}}, 1}
    };
    memcpy(lines, outline, sizeof(outline)); memcpy(regions, triangles, sizeof(triangles));
    return (Scene){.spawn = {2, PLAYER_EYE, 4}, .sector_count = 2, .line_count = 7,
                   .region_count = 4, .sectors = sectors, .lines = lines, .regions = regions};
}

static void synthetic_tests(void) {
    SceneSector sectors[2];
    CollisionLine lines[9];
    CollisionTriangle regions[4];
    Scene scene = room(sectors, lines, regions);
    Player player = player_spawn(&scene);
    player_move(&scene, &player, -20, 0, 0.1f);
    expect(fabsf(player.feet.x - PLAYER_RADIUS) < 0.001f, "solid wall and high-speed movement");
    player = player_spawn(&scene);
    player_move(&scene, &player, -2, 2, 0.1f);
    expect(player.feet.x >= PLAYER_RADIUS && player.feet.z > 5.9f, "slide along wall");
    player = player_spawn(&scene);
    player_move(&scene, &player, -10, -10, 0.1f);
    expect(player.feet.x >= PLAYER_RADIUS - 0.001f && player.feet.z >= PLAYER_RADIUS - 0.001f,
           "corner does not leak");
    player = player_spawn(&scene);
    player_move(&scene, &player, 100, 0, 0.1f);
    expect(fabsf(player.feet.x - (8 - PLAYER_RADIUS)) < 0.001f && player.sector == 1,
           "open portal and far wall");
    sectors[1].floor = PLAYER_STEP;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 1 && fabsf(player.feet.y - PLAYER_STEP) < 1e-6f, "24-unit step");
    sectors[1].floor = 25.0f / 32;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.feet.x <= 4 - PLAYER_RADIUS + 0.001f && player.feet.y == 0, "25-unit ledge blocks");
    sectors[1].floor = 0;
    sectors[1].ceiling = 55.0f / 32;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 0 && player.feet.x < 4, "low ceiling blocks");
    sectors[1].ceiling = PLAYER_HEIGHT;
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 1 && player.feet.y == 0, "exact player headroom passes");
    sectors[1].ceiling = 0;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 0, "closed door blocks");
    sectors[1].ceiling = PLAYER_HEIGHT - 1.0f / 32;
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 0, "partly open door blocks");
    sectors[1].ceiling = 4;
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 1, "changed door ceiling takes effect without rebuilding");
    lines[6].flags = 1;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 0, "impassable two-sided line");
    lines[6].flags = 2;
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 1, "monster-only blocking does not block player");
    lines[6].flags = 0;
    sectors[1].floor = PLAYER_STEP;
    sectors[0].ceiling = 2;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 0, "raised floor and low front ceiling block");
    scene = room(sectors, lines, regions);
    sectors[0].floor = 3; sectors[0].ceiling = 7; sectors[1].ceiling = 7;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.sector == 1 && player.feet.y > 0 && player.feet.y < 3, "fall off ledge");
    player_move(&scene, &player, 0, 0, 1);
    expect(player.feet.y == 0 && player.vertical_speed == 0, "land on floor");
    scene = room(sectors, lines, regions);
    lines[7] = (CollisionLine){{4, 0}, {4, 3.6f}, 0, 1, 1, 0, 0};
    lines[8] = (CollisionLine){{4, 4.4f}, {4, 8}, 0, 1, 1, 0, 0};
    scene.line_count = 9;
    player = player_spawn(&scene);
    player_move(&scene, &player, 4, 0, 0.1f);
    expect(player.feet.x < 4, "narrow gap and line endpoints");
    expect(scene_sector_at(&scene, -1, 4, 0) == UINT32_MAX, "outside region");
    expect(scene_sector_at(&scene, 4, 4, 0) == 0 && scene_sector_at(&scene, 4, 4, 1) == 1,
           "sector hint on shared edge");
    scene = room(sectors, lines, regions);
    for (size_t index = 0; index < scene.line_count; ++index) {
        lines[index].start.x *= 128; lines[index].start.z *= 128;
        lines[index].end.x *= 128; lines[index].end.z *= 128;
    }
    for (size_t index = 0; index < scene.region_count; ++index)
        for (unsigned corner = 0; corner < 3; ++corner) {
            regions[index].points[corner].x *= 128; regions[index].points[corner].z *= 128;
        }
    scene.spawn.x = 128; scene.spawn.z = 128;
    for (unsigned mode = 0; mode < 3; ++mode) {
        player = player_spawn(&scene);
        for (unsigned tick = 0; tick < 200; ++tick) player_tick(&scene, &player, mode == 2 ? 0 : 1,
                                                              mode == 2 ? 1 : 0, 0, mode != 0);
        Position previous = player.feet;
        player_tick(&scene, &player, mode == 2 ? 0 : 1, mode == 2 ? 1 : 0, 0, mode != 0);
        float speed = hypotf(player.feet.x - previous.x, player.feet.z - previous.z) * 32;
        float expected = mode == 0 ? 8.333333f : mode == 1 ? 16.666666f : 13.333333f;
        expect(fabsf(speed - expected) < 0.02f, "Doom walk/run/strafe terminal speed");
        for (unsigned tick = 0; tick < 100; ++tick) player_tick(&scene, &player, 0, 0, 0, false);
        expect(player.velocity_x == 0 && player.velocity_z == 0, "friction comes to rest");
    }
    puts("Passed 19 player movement checks");
    puts("Passed 35 Hz walk/run/strafe speed and friction checks");
}

static void map_test(const char *path) {
    Scene scene = scene_read(path);
    Player player = player_spawn(&scene);
    float start_floor = player.feet.y, lowest_floor = start_floor;
    bool climbed = false;
    for (unsigned tick = 0; tick < 350; ++tick) {
        float previous_floor = player.feet.y;
        player_move(&scene, &player, cosf(scene.yaw) * 7 / 35, -sinf(scene.yaw) * 7 / 35, 1.0f / 35);
        expect(player.sector < scene.sector_count, "MAP01 valid sector");
        expect(player.feet.y >= scene.sectors[player.sector].floor - 0.001f, "MAP01 stays above floor");
        expect(player.feet.y + PLAYER_HEIGHT <= scene.sectors[player.sector].ceiling + 0.001f,
               "MAP01 ceiling clearance");
        lowest_floor = fminf(lowest_floor, player.feet.y);
        climbed = climbed || player.feet.y > previous_floor + 0.01f;
    }
    expect(climbed && lowest_floor < start_floor, "MAP01 entrance drop and stairs");
    Position stopped = player.feet;
    player_move(&scene, &player, cosf(scene.yaw) * 20, -sinf(scene.yaw) * 20, 0.1f);
    expect(hypotf(player.feet.x - stopped.x, player.feet.z - stopped.z) < 0.01f, "MAP01 corridor wall stops player");
    printf("MAP01 walk: start floor %.3f, lowest %.3f, stopped at %.3f %.3f %.3f in sector %u\n",
           start_floor, lowest_floor, player.feet.x, player.feet.y, player.feet.z, player.sector);
    player = player_spawn(&scene);
    uint32_t state = 0x5a17c9u;
    for (unsigned tick = 0; tick < 2000; ++tick) {
        state = state * 1664525u + 1013904223u;
        float angle = (float)(state & 65535) * 0.0000958738f;
        player_move(&scene, &player, cosf(angle), sinf(angle), 0.05f);
        expect(player.feet.y >= scene.sectors[player.sector].floor - 0.001f, "MAP01 random walk floor");
        expect(player.feet.y + PLAYER_HEIGHT <= scene.sectors[player.sector].ceiling + 0.001f,
               "MAP01 random walk ceiling");
        for (size_t index = 0; index < scene.line_count; ++index) {
            const CollisionLine *line = &scene.lines[index];
            if (line->front != UINT32_MAX && line->back != UINT32_MAX && !(line->flags & 1)) continue;
            float dx = line->end.x - line->start.x, dz = line->end.z - line->start.z;
            float fraction = ((player.feet.x - line->start.x) * dx + (player.feet.z - line->start.z) * dz) / (dx * dx + dz * dz);
            fraction = fmaxf(0, fminf(1, fraction));
            float distance = hypotf(player.feet.x - line->start.x - dx * fraction,
                                    player.feet.z - line->start.z - dz * fraction);
            expect(distance >= PLAYER_RADIUS - 0.001f, "MAP01 random walk solid-wall clearance");
        }
    }
    puts("Passed 2000 deterministic MAP01 movement ticks");
    scene_free(&scene);
}

int main(int argc, char **argv) {
    if (argc == 2) map_test(argv[1]);
    else if (argc == 1) synthetic_tests();
    else fail("Usage: check-movement [MAP01.drmap]");
    return 0;
}
