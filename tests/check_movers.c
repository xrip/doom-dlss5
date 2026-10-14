#include "sector_movers.h"

#include <math.h>
#include <stdio.h>

typedef struct {
    Scene scene;
    SceneSector sectors[8];
    CollisionLine lines[12];
    Asset assets[5];
    Batch batches[8];
    WallSurface walls[4];
    Sprite sprites[4];
    SectorMovers movers;
} Fixture;

static unsigned checks;

static void expect(bool result, const char *message) {
    ++checks;
    if (!result) fail("Mover check failed: %s", message);
}

static void height(float actual, float expected, const char *message) {
    expect(fabsf(actual - expected) < 0.00001f, message);
}

static void setup(Fixture *fixture) {
    *fixture = (Fixture){0};
    fixture->sectors[0] = (SceneSector){.floor = 0, .ceiling = 8, .special = 9, .light = 112, .floor_asset = 1};
    fixture->sectors[1] = (SceneSector){.floor = 1, .ceiling = 8, .special = 5, .tag = 7,
                                      .light = 72, .floor_asset = 0, .ceiling_asset = 3};
    fixture->sectors[2] = (SceneSector){.floor = 2, .ceiling = 6, .special = 7, .floor_asset = 2};
    fixture->sectors[3] = (SceneSector){.floor = 3, .ceiling = 7, .floor_asset = 2};
    fixture->lines[0] = (CollisionLine){.front = 0, .back = UINT32_MAX, .tag = 7};
    fixture->lines[1] = (CollisionLine){.start = {4, 0}, .end = {4, 8}, .front = 0, .back = 1};
    fixture->lines[2] = (CollisionLine){.start = {8, 0}, .end = {8, 8}, .front = 1, .back = 2};
    fixture->lines[3] = (CollisionLine){.start = {12, 0}, .end = {12, 8}, .front = 1, .back = 3};
    fixture->assets[0].height = 64;
    fixture->assets[1].height = 32;
    fixture->assets[2].height = 64;
    fixture->assets[3].height = 128;
    fixture->assets[4].height = 8;
    fixture->batches[0] = (Batch){.owner = 1, .plane = SURFACE_FLOOR, .asset = 0};
    fixture->batches[1] = (Batch){.owner = 1, .plane = SURFACE_FLOOR, .asset = 0};
    fixture->batches[2] = (Batch){.owner = 1, .plane = SURFACE_CEILING, .asset = 3};
    fixture->batches[3] = (Batch){.owner = 0, .plane = SURFACE_FLOOR, .asset = 1};
    fixture->batches[4] = (Batch){.owner = 1, .plane = SURFACE_WALL, .asset = 1};
    fixture->batches[5] = (Batch){.owner = 1, .plane = SURFACE_WALL, .asset = 2};
    fixture->batches[6] = (Batch){.owner = 1, .plane = SURFACE_WALL, .asset = 4};
    fixture->batches[7] = (Batch){.owner = 0, .plane = SURFACE_WALL, .asset = 4};
    fixture->walls[0] = (WallSurface){.line = 1, .batch = 4, .kind = WALL_LOWER};
    fixture->walls[1] = (WallSurface){.line = 2, .batch = 5, .kind = WALL_LOWER};
    fixture->walls[2] = (WallSurface){.line = 1, .batch = 6, .kind = WALL_UPPER};
    fixture->walls[3] = (WallSurface){.line = 0, .batch = 7, .kind = WALL_LOWER};
    fixture->sprites[0] = (Sprite){.sector = 1, .position = {6, 1, 4}};
    fixture->sprites[1] = (Sprite){.sector = 1, .position = {6, 8, 4}, .ceiling = true};
    fixture->scene = (Scene){.sectors = fixture->sectors, .sector_count = 4,
                             .lines = fixture->lines, .line_count = 4,
                             .assets = fixture->assets, .asset_count = 5,
                             .batches = fixture->batches, .batch_count = 8,
                             .walls = fixture->walls, .wall_count = 4,
                             .sprites = fixture->sprites, .sprite_count = 2};
    fixture->movers = sector_movers_create(&fixture->scene);
}

static bool start(Fixture *fixture, uint32_t sector, unsigned special) {
    const SectorAction *action = sector_action(special);
    expect(action != NULL, "action exists");
    return sector_movers_start(&fixture->scene, &fixture->movers, sector, action, &fixture->lines[0]);
}

static void ticks(Fixture *fixture, Player *player, unsigned count) {
    for (unsigned tick = 0; tick < count; ++tick) {
        for (uint32_t sector = 0; sector < fixture->scene.sector_count; ++sector)
            sector_movers_tick(&fixture->scene, &fixture->movers, sector, player);
        if (player) ++player->ticks;
    }
}

static void rising_floors(void) {
    const struct { unsigned special, duration; float target, speed; } cases[] = {
        {18, 32, 2, 1}, {131, 8, 2, 4}, {119, 32, 2, 1},
        {5, 160, 6, 1}, {91, 160, 6, 1}, {94, 152, 5.75f, 1},
        {59, 24, 1.75f, 1}, {140, 512, 17, 1}, {30, 32, 2, 1}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        Fixture fixture;
        setup(&fixture);
        expect(start(&fixture, 1, cases[index].special), "rising floor starts");
        height(fixture.movers.floors[1].top, cases[index].target, "rising floor target");
        ticks(&fixture, NULL, 1);
        height(fixture.sectors[1].floor, 1 + cases[index].speed / 32, "rising floor rate");
        ticks(&fixture, NULL, cases[index].duration - 1);
        height(fixture.sectors[1].floor, cases[index].target, "rising floor reaches target on time");
        expect(!sector_movers_busy(&fixture.movers, 1), "completed floor releases sector");
        height(fixture.sprites[0].position.y, cases[index].target, "ground sprite follows rise");
        height(fixture.sprites[1].position.y, 8, "hanging sprite stays during rise");
        sector_movers_free(&fixture.movers);
    }
    Fixture fixture;
    setup(&fixture);
    fixture.sectors[2].floor = 1;
    fixture.sectors[3].floor = 0;
    expect(start(&fixture, 1, 18), "next-floor action accepts no higher neighbor");
    ticks(&fixture, NULL, 1);
    height(fixture.sectors[1].floor, 1, "next-floor action stays when no higher neighbor exists");
    expect(!sector_movers_busy(&fixture.movers, 1), "no-distance floor completes");
    fixture.scene.wall_count = 0;
    expect(!start(&fixture, 1, 30), "texture target requires lower texture");
    fixture.lines[0].tag = 0;
    expect(!start(&fixture, 1, 18), "zero tag rejects action");
    fixture.lines[0].tag = 8;
    expect(!start(&fixture, 1, 18), "different tag rejects action");
    expect(sector_action(13) == NULL && sector_action(35) == NULL, "light actions are outside mover table");
    expect(SECTOR_USE == 0 && SECTOR_WALK == 1 && SECTOR_SHOOT == 2, "trigger numbers stay fixed");
    sector_movers_free(&fixture.movers);
    expect(!fixture.movers.floors && !fixture.movers.ceilings, "free clears movers");
}

static void opposite_targets(void) {
    const unsigned specials[] = {102, 71};
    for (size_t index = 0; index < sizeof(specials) / sizeof(specials[0]); ++index) {
        Fixture fixture;
        setup(&fixture);
        fixture.sectors[0].floor = 2;
        fixture.sectors[2].floor = 2;
        fixture.sectors[3].floor = 2;
        float target = specials[index] == 71 ? 2.25f : 2;
        expect(start(&fixture, 1, specials[index]), "lower-to-higher target starts");
        ticks(&fixture, NULL, 1);
        height(fixture.sectors[1].floor, target, "opposite-direction lower target is instant");
        expect(!sector_movers_busy(&fixture.movers, 1), "instant lower target completes");
        sector_movers_free(&fixture.movers);
    }
    Fixture fixture;
    setup(&fixture);
    fixture.sectors[1].floor = 7;
    fixture.sprites[0].position.y = 7;
    expect(start(&fixture, 1, 5), "raise-to-lower ceiling starts");
    ticks(&fixture, NULL, 1);
    height(fixture.sectors[1].floor, 6, "opposite-direction raise target is instant");
    sector_movers_free(&fixture.movers);
}

static void transfers(void) {
    Fixture fixture;
    setup(&fixture);
    expect(start(&fixture, 1, 59), "trigger model change starts");
    expect(fixture.sectors[1].floor_asset == 1 && fixture.sectors[1].special == 9, "trigger texture and type change at start");
    expect(fixture.batches[0].asset == 1 && fixture.batches[1].asset == 1, "all owner floor batches change");
    expect(fixture.batches[2].asset == 3 && fixture.batches[4].asset == 1 && fixture.batches[5].asset == 2,
           "ceiling and wall batches stay unchanged");
    expect(fixture.sectors[1].light == 72 && fixture.sectors[1].ceiling_asset == 3,
           "model change preserves light and ceiling asset");
    height(fixture.sectors[1].floor, 1, "trigger transfer does not move floor at start");
    sector_movers_free(&fixture.movers);
    setup(&fixture);
    fixture.sectors[2].floor = 0;
    expect(start(&fixture, 1, 37), "numeric model lowering starts");
    expect(fixture.sectors[1].floor_asset == 0 && fixture.sectors[1].special == 5, "numeric change waits for finish");
    ticks(&fixture, NULL, 31);
    expect(fixture.sectors[1].floor_asset == 0, "numeric texture stays until target reached");
    ticks(&fixture, NULL, 1);
    expect(fixture.sectors[1].floor_asset == 1 && fixture.sectors[1].special == 9,
           "lowest numbered matching boundary supplies numeric model");
    expect(fixture.batches[0].asset == 1 && fixture.batches[1].asset == 1 && fixture.sectors[1].light == 72,
           "numeric transfer updates floor batches and preserves light");
    sector_movers_free(&fixture.movers);
    setup(&fixture);
    fixture.sectors[1].floor = -1;
    expect(start(&fixture, 1, 37), "numeric change with no model starts");
    ticks(&fixture, NULL, 1);
    expect(fixture.sectors[1].floor_asset == 0 && fixture.sectors[1].special == 5,
           "missing numeric model preserves texture and type");
    sector_movers_free(&fixture.movers);
}

static void platforms(void) {
    const struct { unsigned special, duration; float target; } cases[] = {
        {15, 48, 1.75f}, {67, 64, 2}, {68, 64, 2}, {20, 64, 2}, {22, 64, 2}, {47, 64, 2}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        Fixture fixture;
        setup(&fixture);
        Player player = {.feet = {6, 1, 4}, .sector = 1, .health = 100};
        expect(start(&fixture, 1, cases[index].special), "change platform starts");
        expect(fixture.sectors[1].floor_asset == 1, "platform texture changes at start");
        expect(fixture.sectors[1].special == (cases[index].special == 15 ? 5u : 0u), "platform type change matches table");
        expect(!start(&fixture, 1, 18), "platform excludes another floor action");
        expect(!start(&fixture, 1, 49), "platform excludes ceiling action");
        ticks(&fixture, &player, 1);
        height(fixture.sectors[1].floor, 1 + 0.5f / 32, "change platform half-unit rate");
        ticks(&fixture, &player, cases[index].duration - 1);
        height(fixture.sectors[1].floor, cases[index].target, "change platform target and time");
        height(player.feet.y, cases[index].target, "player rides change platform");
        expect(!sector_movers_busy(&fixture.movers, 1), "change platform stops at top");
        sector_movers_free(&fixture.movers);
    }
    expect(sector_action(47)->trigger == SECTOR_SHOOT && !sector_action(47)->repeat, "gun platform is shoot-once");
    Fixture fixture;
    setup(&fixture);
    Player player = {.feet = {6, 1, 4}, .sector = 1, .health = 100};
    fixture.sectors[1].ceiling = 1 + PLAYER_HEIGHT;
    expect(start(&fixture, 1, 68), "blocked change platform starts");
    ticks(&fixture, &player, 1);
    expect(fixture.movers.floors[1].state == FLOOR_LOWERING, "blocked platform reverses");
    height(fixture.sectors[1].floor, 1, "blocked platform does not enter player");
    expect(player.health == 100, "non-crushing platform causes no damage");
    ticks(&fixture, &player, 1);
    expect(fixture.movers.floors[1].wait == 105, "reversed platform waits at bottom");
    fixture.sectors[1].ceiling = 8;
    ticks(&fixture, &player, 105 + 64);
    height(fixture.sectors[1].floor, 2, "platform retries after clearance returns");
    sector_movers_free(&fixture.movers);
}

static void perpetual(void) {
    Fixture fixture;
    setup(&fixture);
    expect(start(&fixture, 1, 87), "perpetual platform starts");
    expect(fixture.movers.floors[1].state == FLOOR_LOWERING, "perpetual initial direction is deterministic down");
    ticks(&fixture, NULL, 32);
    height(fixture.sectors[1].floor, 0, "perpetual reaches lowest neighbor");
    expect(fixture.movers.floors[1].state == FLOOR_WAITING && fixture.movers.floors[1].wait == 105,
           "perpetual waits at bottom");
    ticks(&fixture, NULL, 5);
    expect(start(&fixture, 1, 89), "stop pauses perpetual wait");
    expect(sector_movers_busy(&fixture.movers, 1), "paused perpetual keeps ownership");
    expect(!start(&fixture, 1, 89) && !start(&fixture, 1, 18), "paused platform rejects duplicate stop and other floor");
    ticks(&fixture, NULL, 200);
    expect(fixture.movers.floors[1].wait == 100, "pause freezes wait timer");
    expect(start(&fixture, 1, 87), "perpetual resumes saved wait");
    ticks(&fixture, NULL, 100);
    expect(fixture.movers.floors[1].state == FLOOR_RISING, "saved wait expires at original time");
    ticks(&fixture, NULL, 16);
    expect(start(&fixture, 1, 89), "stop pauses rising perpetual");
    float paused = fixture.sectors[1].floor;
    ticks(&fixture, NULL, 20);
    height(fixture.sectors[1].floor, paused, "pause freezes platform height");
    expect(start(&fixture, 1, 87), "perpetual resumes saved rise");
    ticks(&fixture, NULL, 80);
    height(fixture.sectors[1].floor, 3, "perpetual reaches highest neighbor");
    expect(fixture.movers.floors[1].wait == 105, "perpetual waits at top");
    ticks(&fixture, NULL, 105 + 96);
    height(fixture.sectors[1].floor, 0, "perpetual completes another downward stroke");
    expect(!start(&fixture, 1, 87), "active perpetual rejects restart");
    sector_movers_free(&fixture.movers);
}

static void stairs(void) {
    const unsigned specials[] = {7, 8, 100, 127};
    for (size_t index = 0; index < sizeof(specials) / sizeof(specials[0]); ++index) {
        Fixture fixture;
        setup(&fixture);
        fixture.scene.sector_count = 6;
        for (uint32_t sector = 2; sector < 6; ++sector)
            fixture.sectors[sector] = (SceneSector){.floor = 1, .ceiling = 8, .floor_asset = sector == 5 ? 1u : 0u};
        fixture.lines[1] = (CollisionLine){.front = 4, .back = 1};
        fixture.lines[2] = (CollisionLine){.front = 1, .back = 5};
        fixture.lines[3] = (CollisionLine){.front = 1, .back = 2};
        fixture.lines[4] = (CollisionLine){.front = 1, .back = 4};
        fixture.lines[5] = (CollisionLine){.front = 2, .back = 3};
        fixture.lines[6] = (CollisionLine){.front = 3, .back = 1};
        fixture.lines[7] = (CollisionLine){.front = 3, .back = 5};
        fixture.scene.line_count = 8;
        sector_movers_free(&fixture.movers);
        fixture.movers = sector_movers_create(&fixture.scene);
        float step = specials[index] >= 100 ? 0.5f : 0.25f;
        unsigned first_duration = specials[index] >= 100 ? 4u : 32u;
        expect(start(&fixture, 1, specials[index]), "tagged stair root starts");
        height(fixture.movers.floors[1].top, 1 + step, "stair root target");
        height(fixture.movers.floors[2].top, 1 + step * 2, "stair follows first forward matching boundary");
        height(fixture.movers.floors[3].top, 1 + step * 3, "stair chain increases target each step");
        expect(!sector_movers_busy(&fixture.movers, 4) && !sector_movers_busy(&fixture.movers, 5),
               "later branch and different flat stay idle");
        expect(!start(&fixture, 1, specials[index]), "busy stair root rejects another start");
        ticks(&fixture, NULL, first_duration);
        expect(fixture.movers.floors[1].state == FLOOR_STOPPED && fixture.movers.floors[3].state == FLOOR_RISING,
               "stair motion starts together with different completion times");
        ticks(&fixture, NULL, first_duration * 2);
        for (uint32_t sector = 1; sector <= 3; ++sector) {
            height(fixture.sectors[sector].floor, 1 + step * (float)sector, "ordered stair final height");
            expect(!sector_movers_busy(&fixture.movers, sector), "stair chain ends without cycle");
        }
        sector_movers_free(&fixture.movers);
    }
}

static void floor_obstruction(void) {
    const unsigned specials[] = {18, 94};
    for (size_t index = 0; index < sizeof(specials) / sizeof(specials[0]); ++index) {
        Fixture fixture;
        setup(&fixture);
        fixture.sectors[1].ceiling = 1 + PLAYER_HEIGHT;
        Player player = {.feet = {6, 1, 4}, .sector = 1, .health = 100};
        expect(start(&fixture, 1, specials[index]), "obstructed floor starts");
        ticks(&fixture, &player, 1);
        height(fixture.sectors[1].floor, 1, "obstructed floor does not penetrate player");
        expect(fixture.movers.floors[1].state == FLOOR_RISING,
               "ordinary and crushing floors stall without reversing");
        expect(player.health == (specials[index] == 94 ? 90u : 100u), "floor damage requires crush flag");
        player.sector = 0;
        player.feet = (Position){2, 0, 4};
        ticks(&fixture, &player, 1);
        expect(fixture.sectors[1].floor > 1,
               "stalled floor continues after player leaves");
        sector_movers_free(&fixture.movers);
    }
}

static void crushers(void) {
    const unsigned specials[] = {49, 73, 77, 141};
    for (size_t index = 0; index < sizeof(specials) / sizeof(specials[0]); ++index) {
        Fixture fixture;
        setup(&fixture);
        unsigned duration = specials[index] == 77 ? 108u : 216u;
        float speed = specials[index] == 77 ? 2.0f : 1.0f;
        expect(start(&fixture, 1, specials[index]), "crusher starts");
        ticks(&fixture, NULL, 1);
        height(fixture.sectors[1].ceiling, 8 - speed / 32, "crusher descent rate");
        ticks(&fixture, NULL, duration - 1);
        height(fixture.sectors[1].ceiling, 1.25f, "crusher reaches eight above floor");
        expect(fixture.movers.ceilings[1].state == FLOOR_RISING, "crusher reverses without bottom wait");
        height(fixture.sprites[1].position.y, 1.25f, "hanging sprite follows crusher");
        height(fixture.sprites[0].position.y, 1, "ground sprite stays during ceiling motion");
        ticks(&fixture, NULL, duration);
        height(fixture.sectors[1].ceiling, 8, "crusher returns to original ceiling");
        expect(fixture.movers.ceilings[1].state == FLOOR_LOWERING, "crusher immediately starts next stroke");
        expect(start(&fixture, 1, 74), "crusher stop succeeds");
        expect(!start(&fixture, 1, 74) && !start(&fixture, 1, 18), "paused crusher retains sector ownership");
        ticks(&fixture, NULL, 20);
        height(fixture.sectors[1].ceiling, 8, "paused crusher does not move");
        expect(start(&fixture, 1, specials[index] == 77 ? 49u : 77u), "different crusher action resumes saved mover");
        height(fixture.movers.ceilings[1].speed, speed / 32, "restart preserves original crusher speed");
        ticks(&fixture, NULL, 1);
        height(fixture.sectors[1].ceiling, 8 - speed / 32, "resumed crusher keeps original stroke");
        sector_movers_free(&fixture.movers);
    }
}

static void crusher_obstruction(void) {
    const unsigned specials[] = {49, 77};
    for (size_t index = 0; index < sizeof(specials) / sizeof(specials[0]); ++index) {
        Fixture fixture;
        setup(&fixture);
        fixture.sectors[1].ceiling = 1 + PLAYER_HEIGHT;
        fixture.sprites[1].position.y = fixture.sectors[1].ceiling;
        Player player = {.feet = {3.75f, 1, 4}, .sector = 0, .health = 100};
        expect(start(&fixture, 1, specials[index]), "blocked crusher starts");
        ticks(&fixture, &player, 1);
        expect(fixture.movers.ceilings[1].state == FLOOR_LOWERING,
               "crusher detects player radius across boundary and stalls");
        height(fixture.sectors[1].ceiling, 1 + PLAYER_HEIGHT, "blocked crusher does not enter player");
        expect(player.health == 90, "blocked crusher gives simple ten-health damage");
        ticks(&fixture, &player, 3);
        expect(player.health == 90, "crusher damage waits four player ticks");
        ticks(&fixture, &player, 1);
        expect(player.health == 80, "crusher damage repeats on fourth tick");
        expect(fixture.movers.ceilings[1].slowed == (specials[index] == 49), "only slow crusher slows on obstruction");
        expect(start(&fixture, 1, 74), "blocked crusher can pause");
        ticks(&fixture, &player, 8);
        expect(player.health == 80, "paused crusher causes no damage");
        expect(start(&fixture, 1, 73), "blocked crusher resumes");
        player.feet = (Position){2, 0, 4};
        ticks(&fixture, &player, 1);
        float speed = specials[index] == 49 ? 1.0f / 256 : 2.0f / 32;
        height(fixture.sectors[1].ceiling, 1 + PLAYER_HEIGHT - speed, "crusher continues at saved rate after player leaves");
        sector_movers_free(&fixture.movers);
    }
    Fixture fixture;
    setup(&fixture);
    fixture.sectors[1].ceiling = 1 + PLAYER_HEIGHT;
    Player player = {.feet = {6, 1, 4}, .sector = 1, .health = 5};
    expect(start(&fixture, 1, 49), "low-health obstruction starts");
    ticks(&fixture, &player, 8);
    expect(player.health == 0, "damage saturates at zero");
    height(fixture.sectors[1].ceiling, 1 + PLAYER_HEIGHT, "non-penetration does not claim dead-body combat parity");
    sector_movers_free(&fixture.movers);
}

int main(void) {
    rising_floors();
    opposite_targets();
    transfers();
    platforms();
    perpetual();
    stairs();
    floor_obstruction();
    crushers();
    crusher_obstruction();
    printf("Passed %u synthetic mover checks\n", checks);
    return 0;
}
