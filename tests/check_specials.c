#include "specials.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(bool result, const char *message) {
    if (!result) fail("Interaction check failed: %s", message);
}

static void ticks(Scene *scene, Specials *specials, Player *player, unsigned count) {
    for (unsigned tick = 0; tick < count; ++tick) specials_tick(scene, specials, player);
}

static Player front_player(const Scene *scene, const CollisionLine *line, float *yaw) {
    float dx = line->end.x - line->start.x, dz = line->end.z - line->start.z;
    float length = hypotf(dx, dz), nx = -dz / length, nz = dx / length;
    *yaw = atan2f(nz, -nx);
    return (Player){.feet = {(line->start.x + line->end.x) / 2 + nx * 0.5f,
        scene->sectors[line->front].floor, (line->start.z + line->end.z) / 2 + nz * 0.5f},
        .sector = line->front, .view_height = PLAYER_EYE, .health = 100};
}

static void synthetic(void) {
    SceneSector sectors[] = {{.ceiling = 8, .light = 128}, {.ceiling = 8, .light = 64, .special = 9},
                             {.floor = 2, .ceiling = 8, .light = 192, .tag = 9}};
    CollisionLine line = {.start = {3, 0}, .end = {3, 8}, .front = 0, .back = 1, .special = 39, .tag = 9};
    Teleport teleport = {.position = {10, 2, 4}, .yaw = 1.5f, .sector = 2};
    Scene scene = {.sectors = sectors, .sector_count = 3, .lines = &line, .line_count = 1,
                   .teleports = &teleport, .teleport_count = 1, .map = "MAP01"};
    Specials specials = specials_create(&scene);
    Player player = {.feet = {2, 0, 4}, .sector = 0, .health = 100, .velocity_x = 1, .vertical_speed = -1};
    line.tag = 99;
    expect(!specials_cross(&scene, &specials, player.feet, (Position){4, 0, 4}, &player) && player.feet.x == 2,
           "teleport without an authored destination is a no-op");
    line.tag = 9;
    expect(!specials_cross(&scene, &specials, (Position){4, 0, 4}, player.feet, &player) && line.special == 39,
           "teleport only works front to back");
    expect(specials_cross(&scene, &specials, player.feet, (Position){4, 0, 4}, &player), "player teleport crosses");
    expect(player.feet.x == 10 && player.feet.y == 2 && player.sector == 2 && specials.teleport_yaw == 1.5f,
           "teleport uses marker floor and angle");
    expect(player.teleport_wait == 18 && player.velocity_x == 0 && player.vertical_speed == 0 && line.special == 0,
           "teleport delay, momentum reset, and once behavior");
    player.sector = 1; player.feet.y = 1;
    specials_player(&scene, &specials, &player);
    expect(specials.secrets == 0 && specials.total_secrets == 1, "airborne secrets do not count");
    player.feet.y = 0;
    specials_player(&scene, &specials, &player); specials_player(&scene, &specials, &player);
    expect(specials.secrets == 1 && sectors[1].special == 0, "secret counted once on floor");
    sectors[1].special = 5; specials.ticks = 31;
    specials_player(&scene, &specials, &player);
    expect(player.health == 100, "damage waits for tick boundary");
    specials.ticks = 32; specials_player(&scene, &specials, &player);
    expect(player.health == 90, "damage floor changes health");
    player.feet = (Position){2, 0, 4}; player.sector = 0;
    line.special = 138; line.tag = 9;
    expect(specials_use(&scene, &specials, &player, 0) && sectors[2].light == 255, "light switch changes tagged sector");
    line.special = 139;
    expect(specials_use(&scene, &specials, &player, 0) && sectors[2].light == 35, "dark light switch repeats");
    line.special = 2; line.tag = 9; line.back = 2; sectors[2].floor = 0; sectors[2].ceiling = 0;
    expect(!specials_cross(&scene, &specials, (Position){2, 0, 0}, (Position){3, 0, 0}, &player), "walk action is not teleport");
    expect(specials.doors[2].state == DOOR_STOPPED, "touching line from same side does not trigger");
    specials_cross(&scene, &specials, (Position){3, 0, 4}, (Position){4, 0, 4}, &player);
    expect(specials.doors[2].state == DOOR_OPENING && line.special == 0, "crossing from exactly on line triggers");
    line.special = 11; line.tag = 0;
    expect(specials_use(&scene, &specials, &player, 0) && specials.exited && line.special == 0,
           "untagged exit switch completes map");
    char *next = specials_next_map(&scene, &specials, "C:/maps/custom.drmap");
    expect(!strcmp(next, "C:/maps/MAP02.drmap"), "next map uses sibling prepared file"); free(next);
    memcpy(scene.map, "MAP31", 6); specials.secret_exit = true;
    next = specials_next_map(&scene, &specials, "maps\\MAP31.drmap");
    expect(!strcmp(next, "maps\\MAP32.drmap"), "secret map routing"); free(next);
    specials.secret_exit = false;
    next = specials_next_map(&scene, &specials, "maps/MAP31.drmap");
    expect(!strcmp(next, "maps/MAP16.drmap"), "secret level normal return"); free(next);
    memcpy(scene.map, "MAP30", 6);
    expect(!specials_next_map(&scene, &specials, "MAP30.drmap"), "final level has no next map");
    specials_free(&specials);
    sectors[1].special = 8; sectors[1].light = 128; sectors[0].light = 32; line.back = 1;
    sectors[2].special = 10; sectors[2].ceiling = 4;
    specials = specials_create(&scene);
    ticks(&scene, &specials, NULL, 1);
    expect(sectors[1].light == 120, "glowing sector changes light");
    ticks(&scene, &specials, NULL, 1049);
    expect(specials.doors[2].state == DOOR_CLOSING && sectors[2].ceiling == 4, "timed sector door waits thirty seconds");
    ticks(&scene, &specials, NULL, 1);
    expect(sectors[2].ceiling == 4 - 2.0f / 32, "timed door closes at ordinary speed");
    specials_free(&specials);
    sectors[2].special = 0; line.back = 2; line.tag = 9; line.special = 42;
    specials = specials_create(&scene);
    specials.doors[2].state = DOOR_OPEN;
    expect(specials_use(&scene, &specials, &player, 0) && specials.doors[2].state == DOOR_CLOSING,
           "open-stay door can later be closed by another trigger");
    line.special = 103;
    expect(!specials_use(&scene, &specials, &player, 0) && line.special == 103,
           "busy tagged door does not consume one-shot switch");
    specials_free(&specials);
}

static void snapshot(Scene *scene, const char *prefix, const char *suffix, Player player, float yaw) {
    if (!prefix) return;
    scene->spawn = player.feet; scene->spawn.y += PLAYER_EYE; scene->yaw = yaw;
    char *path = scene_sidecar(prefix, suffix);
    scene_write(scene, path); free(path);
}

static void map01(const char *path, const char *prefix) {
    const unsigned door_lines[] = {3, 47, 75, 124, 166, 167, 170, 173};
    const unsigned door_sectors[] = {31, 55, 42, 22, 37, 37, 29, 29};
    for (size_t index = 0; index < sizeof(door_lines) / sizeof(door_lines[0]); ++index) {
        Scene scene = scene_read(path);
        Specials specials = specials_create(&scene);
        float yaw;
        Player player = front_player(&scene, &scene.lines[door_lines[index]], &yaw);
        expect(specials_use(&scene, &specials, &player, yaw), "every MAP01 door/switch has a working use ray");
        expect(specials.doors[door_sectors[index]].state == DOOR_OPENING, "MAP01 use targets correct door sector");
        specials_free(&specials); scene_free(&scene);
    }
    Scene scene = scene_read(path);
    Specials specials = specials_create(&scene);
    float yaw;
    Player player = front_player(&scene, &scene.lines[126], &yaw);
    expect(specials_use(&scene, &specials, &player, yaw), "MAP01 lift use");
    ticks(&scene, &specials, NULL, 28);
    expect(scene.sectors[21].floor == 40.0f / 32 && scene.sectors[22].ceiling == 152.0f / 32 &&
           specials.doors[22].state == DOOR_STOPPED && scene.lines[124].special == 31,
           "lowering lift does not open nearby imp door");
    for (size_t index = 0; index < scene.wall_count; ++index) {
        const WallSurface *wall = &scene.walls[index];
        if (wall->line != 124 || wall->side) continue;
        SceneVertex vertices[6]; wall_vertices(&scene, wall, vertices);
        float bottom = vertices[0].y, top = vertices[2].y;
        expect(wall->kind == WALL_UPPER ? bottom == 152.0f / 32 && top == 232.0f / 32 :
               bottom == 40.0f / 32 && top == 152.0f / 32, "secret door remains covered above lowered lift");
    }
    player = front_player(&scene, &scene.lines[84], &yaw);
    expect(specials_use(&scene, &specials, &player, yaw), "MAP01 barrier use");
    ticks(&scene, &specials, NULL, 64);
    expect(scene.sectors[55].floor == scene.sectors[55].ceiling && specials.doors[55].state == DOOR_STOPPED,
           "lowering north barriers does not open plasma room door");
    CollisionLine *grate = &scene.lines[242];
    expect(grate->special == 2 && grate->tag == 6, "MAP01 grate west-edge trigger metadata");
    Player alcove_view = front_player(&scene, &scene.lines[51], &yaw);
    snapshot(&scene, prefix, "-grate-before.drmap", alcove_view, yaw);
    Player falling = {.feet = {12.75f, 152.0f / 32, -21}, .sector = 20, .health = 100};
    Position above = falling.feet;
    player_move(&scene, &falling, -1, 0, 1.0f / 35);
    expect(falling.feet.x < 12 && falling.feet.y > 72.0f / 32, "falling player crosses grate edge with collision active");
    specials_cross(&scene, &specials, above, falling.feet, &falling);
    expect(specials.doors[58].state == DOOR_OPENING && scene.lines[242].special == 0,
           "airborne west-edge crossing opens rocket alcove once");
    ticks(&scene, &specials, NULL, 200);
    expect(scene.sectors[58].ceiling > scene.sectors[58].floor + PLAYER_HEIGHT && specials.doors[58].state == DOOR_OPEN,
           "grate secret becomes passable and stays open");
    snapshot(&scene, prefix, "-grate-after.drmap", alcove_view, yaw);
    expect(scene.sectors[31].ceiling == scene.sectors[31].floor, "grate does not open separate courtyard door");
    const unsigned secret_sectors[] = {8, 17, 28, 41, 57};
    for (size_t index = 0; index < sizeof(secret_sectors) / sizeof(secret_sectors[0]); ++index) {
        player.sector = secret_sectors[index]; player.feet.y = scene.sectors[player.sector].floor;
        specials_player(&scene, &specials, &player);
    }
    expect(specials.secrets == 5 && specials.total_secrets == 5, "all five MAP01 secret sectors count");
    player = front_player(&scene, &scene.lines[162], &yaw);
    snapshot(&scene, prefix, "-exit.drmap", player, yaw);
    expect(!specials_shoot(&scene, &specials, &player, yaw), "MAP01 exit is not a shoot trigger");
    expect(specials_use(&scene, &specials, &player, yaw) && specials.exited, "MAP01 exit switch completes level");
    specials_free(&specials); scene_free(&scene);
}

static void coverage(const char *path) {
    Scene scene = scene_read(path);
    unsigned unbound = 0;
    for (size_t index = 0; index < scene.line_count; ++index) {
        unsigned special = scene.lines[index].special;
        if (special && !specials_supported(special)) fail("Unsupported %s line %zu action %u", scene.map, index, special);
        if (special == 39 || special == 97) {
            bool found = false;
            for (size_t marker = 0; marker < scene.teleport_count; ++marker)
                found = found || scene.sectors[scene.teleports[marker].sector].tag == scene.lines[index].tag;
            if (!found) ++unbound;
        }
    }
    for (size_t index = 0; index < scene.sector_count; ++index) {
        unsigned special = scene.sectors[index].special;
        if (special > 17 || special == 6 || special == 15)
            fail("Unsupported %s sector %zu effect %u", scene.map, index, special);
    }
    Specials specials = specials_create(&scene);
    ticks(&scene, &specials, NULL, 500);
    if (unbound) printf("%s: %u authored teleport lines have no matching destination\n", scene.map, unbound);
    specials_free(&specials); scene_free(&scene);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[2], "--coverage")) coverage(argv[1]);
    else {
        if (argc > 3) fail("Usage: check-specials [MAP01.drmap [snapshot-prefix]] | MAPxx.drmap --coverage");
        synthetic();
        if (argc >= 2) map01(argv[1], argc == 3 ? argv[2] : NULL);
    }
    puts("Passed triggers, teleports, secrets, exits, lights, and MAP01 action isolation");
    return 0;
}
