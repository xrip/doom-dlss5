#include "lighting.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(bool condition, const char *message) {
    if (condition) return;
    fprintf(stderr, "Lighting check failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void sector_sources(void) {
    SceneSector sectors[] = {
        {.floor = 0, .ceiling = 8, .light = 200, .ceiling_asset = 0},
        {.floor = 0, .ceiling = 8, .light = 80, .ceiling_asset = 0}
    };
    CollisionLine lines[] = {{.front = 0, .back = 1}};
    CollisionTriangle regions[] = {
        {.points = {{0, 0}, {6, 0}, {0, 6}}, .sector = 0},
        {.points = {{10, 10}, {11, 10}, {10, 11}}, .sector = 0},
        {.points = {{8, 0}, {9, 0}, {8, 1}}, .sector = 1}
    };
    Scene scene = {.sector_count = 2, .line_count = 1, .region_count = 3,
                   .sectors = sectors, .lines = lines, .regions = regions};
    SceneLights *lights = scene_lights_create(&scene);
    RayLight output[MAX_RAY_LIGHTS];
    unsigned count = scene_lights_collect(lights, &scene, (Position){2, 1, 2}, output);
    expect(count == 1, "only brighter adjacent sector emits");
    expect(output[0].meta[0] == 1 && output[0].meta[1] == 0, "sector light metadata");
    expect(fabsf(output[0].position[0] - 2.0f) < 0.0001f &&
           fabsf(output[0].position[2] - 2.0f) < 0.0001f, "largest triangle centroid stays inside sector");
    expect(fabsf(output[0].position[1] - 7.85f) < 0.0001f, "sector ceiling height is current");
    sectors[0].light = 100;
    expect(scene_lights_collect(lights, &scene, (Position){2, 1, 2}, output) == 0,
           "current sector light removes lost contrast");
    sectors[0].light = 140;
    expect(scene_lights_collect(lights, &scene, (Position){2, 1, 2}, output) == 1,
           "current sector light restores contrast");
    sectors[0].ceiling_asset = UINT32_MAX;
    expect(scene_lights_collect(lights, &scene, (Position){2, 1, 2}, output) == 0,
           "sky ceiling has no source");
    sectors[0].ceiling_asset = 0;
    sectors[0].ceiling = sectors[0].floor;
    expect(scene_lights_collect(lights, &scene, (Position){2, 1, 2}, output) == 0,
           "closed sector has no source");
    scene_lights_destroy(lights);
}

static void sprite_sources(void) {
    uint8_t blue_pixel[] = {1, 0, 0, 255};
    uint8_t red_pixel[] = {2, 0, 0, 255};
    uint8_t green_pixel[] = {3, 0, 0, 255};
    Asset assets[] = {
        {.name = "TLMPA0", .width = 1, .height = 1, .pixels = blue_pixel},
        {.name = "TLMPB0", .width = 1, .height = 1, .pixels = red_pixel},
        {.name = "ARM1A0", .width = 1, .height = 1, .pixels = green_pixel},
        {.name = "BAR1A0", .width = 1, .height = 1, .pixels = green_pixel}
    };
    Sprite sprites[] = {
        {.asset = 0, .position = {1, 2, 3}, .fullbright = true, .frame_count = 2,
         .frame_index = 0, .frame_ticks = 1,
         .frames = {{.asset = 0, .ticks = 1, .fullbright = true},
                    {.asset = 1, .ticks = 1, .fullbright = true}}},
        {.asset = 2, .position = {4, 5, 6}},
        {.asset = 3, .position = {7, 8, 9}, .fullbright = true}
    };
    Scene scene = {.asset_count = 4, .sprite_count = 3, .assets = assets, .sprites = sprites};
    scene.lut[4] = 0; scene.lut[5] = 0; scene.lut[6] = 255; scene.lut[7] = 255;
    scene.lut[8] = 255; scene.lut[9] = 0; scene.lut[10] = 0; scene.lut[11] = 255;
    scene.lut[12] = 0; scene.lut[13] = 255; scene.lut[14] = 0; scene.lut[15] = 255;
    SceneLights *lights = scene_lights_create(&scene);
    RayLight output[MAX_RAY_LIGHTS];
    unsigned count = scene_lights_collect(lights, &scene, (Position){0}, output);
    expect(count == 1 && output[0].meta[1] == 1, "non-glowing and non-fullbright sprites stay dark");
    expect(output[0].color[2] > 0.99f && output[0].color[0] < 0.01f,
           "source color comes from palette texels in linear RGB");
    expect(output[0].position[0] == 1 && output[0].position[2] == 3,
           "sprite position follows the live scene");
    sprites[0].position = (Position){11, 12, 13};
    scene_tick_sprites(&scene);
    count = scene_lights_collect(lights, &scene, (Position){0}, output);
    expect(count == 1 && output[0].color[0] > 0.99f && output[0].color[2] < 0.01f,
           "animated frame changes emitted color");
    expect(output[0].position[0] == 11 && output[0].position[2] == 13,
           "moving sprite moves its emitted light");
    sprites[0].hidden = true;
    expect(scene_lights_collect(lights, &scene, (Position){0}, output) == 0,
           "hidden animated sprite stops emitting");
    sprites[1].fullbright = true;
    expect(scene_lights_collect(lights, &scene, (Position){0}, output) == 1 &&
           output[0].meta[1] == 2, "flashing armor emits only on fullbright frame");
    scene_lights_destroy(lights);
}

static void source_limit(void) {
    uint8_t pixels[] = {1, 0, 0, 255};
    Asset assets[] = {
        {.name = "FCANA0", .width = 1, .height = 1, .pixels = pixels},
        {.name = "TLMPA0", .width = 1, .height = 1, .pixels = pixels}
    };
    Sprite sprites[33] = {0};
    for (size_t index = 0; index < 32; ++index)
        sprites[index] = (Sprite){.asset = 0, .position = {0, 0, 0}};
    sprites[32] = (Sprite){.asset = 1, .position = {1000, 0, 0}};
    Scene scene = {.asset_count = 2, .sprite_count = 33, .assets = assets, .sprites = sprites};
    scene.lut[4] = 255; scene.lut[5] = 255; scene.lut[6] = 255; scene.lut[7] = 255;
    SceneLights *lights = scene_lights_create(&scene);
    RayLight first[MAX_RAY_LIGHTS], second[MAX_RAY_LIGHTS];
    unsigned count = scene_lights_collect(lights, &scene, (Position){0}, first);
    expect(count == MAX_RAY_LIGHTS, "collection never exceeds contract capacity");
    bool found_far_lamp = false;
    for (unsigned index = 0; index < count; ++index)
        found_far_lamp |= first[index].meta[1] == 33;
    expect(!found_far_lamp, "distant sources with no local effect do not displace nearby lights");
    expect(scene_lights_collect(lights, &scene, (Position){0}, second) == count,
           "repeated collection has stable count");
    for (unsigned index = 0; index < count; ++index)
        expect(first[index].meta[1] == second[index].meta[1], "repeated collection ranks deterministically");
    scene_lights_destroy(lights);
}

int main(void) {
    sector_sources();
    sprite_sources();
    source_limit();
    puts("Passed natural sector and sprite lighting checks");
    return 0;
}
