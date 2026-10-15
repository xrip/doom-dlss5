#include "renderer.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(bool condition, const char *message) {
    if (!condition) fail("Renderer check failed: %s", message);
}

static double pixel_difference(const uint8_t *first, const uint8_t *second, size_t bytes) {
    double difference = 0;
    for (size_t index = 0; index < bytes; ++index) difference += abs((int)first[index] - (int)second[index]);
    return difference / (double)bytes;
}

static float motion(Renderer *renderer) {
    float value = renderer_motion_stats(renderer);
    expect(isfinite(value) && value >= 0, "GPU motion statistic is finite and non-negative");
    return value;
}

static void expect_static(Renderer *renderer, const char *message) {
    expect(motion(renderer) <= 0.00001f, message);
}

static void expect_motion(Renderer *renderer, const char *message) {
    expect(motion(renderer) > 0.0001f, message);
}

static Player switch_player(const Scene *scene, const CollisionLine *line, float *yaw) {
    float dx = line->end.x - line->start.x;
    float dz = line->end.z - line->start.z;
    float length = hypotf(dx, dz);
    float normal_x = -dz / length;
    float normal_z = dx / length;
    *yaw = atan2f(normal_z, -normal_x);
    return (Player){
        .feet = {(line->start.x + line->end.x) * 0.5f + normal_x * 0.5f,
                 scene->sectors[line->front].floor,
                 (line->start.z + line->end.z) * 0.5f + normal_z * 0.5f},
        .sector = line->front,
        .view_height = PLAYER_EYE
    };
}

static RenderCamera camera_at(const Player *player, float yaw) {
    Position position = {player->feet.x, player->feet.y + PLAYER_EYE, player->feet.z};
    return (RenderCamera){
        .position = position,
        .target = {position.x + cosf(yaw), position.y, position.z - sinf(yaw)},
        .fovy = 70
    };
}

static void check_billboards(GLFWwindow *window) {
    uint8_t texels[32 * 32 * 4] = {0};
    for (unsigned index = 0; index < 32 * 32; ++index) {
        texels[index * 4] = index % 32 < 16 ? 1 : 2;
        texels[index * 4 + 3] = 255;
    }
    uint8_t next_texels[sizeof(texels)];
    memcpy(next_texels, texels, sizeof(texels));
    for (unsigned index = 0; index < 32 * 32; ++index)
        next_texels[index * 4] = texels[index * 4] == 1 ? 2 : 1;
    Asset assets[] = {
        {.width = 32, .height = 32, .left = 16, .top = 32, .pixels = texels},
        {.width = 32, .height = 32, .left = 16, .top = 32, .pixels = next_texels}
    };
    Sprite sprite = {.fullbright = true, .frame_count = 2, .frame_ticks = 6,
        .frames = {{.asset = 0, .ticks = 6, .fullbright = true},
                   {.asset = 1, .ticks = 6, .fullbright = true}}};
    Scene scene = {.asset_count = 2, .assets = assets, .sprite_count = 1, .sprites = &sprite};
    scene.lut[4] = 64;
    scene.lut[7] = 255;
    scene.lut[9] = 96;
    scene.lut[11] = 255;
    Specials specials = {0};
    Renderer *renderer = renderer_create(window, &scene, (RendererOptions){.validation = true});
    size_t baseline = 0;
    for (unsigned angle = 0; angle < 8; ++angle) {
        float yaw = (float)angle * 0.7853981633974483f;
        RenderCamera camera = {.position = {4 * cosf(yaw), 0.5f, 4 * sinf(yaw)},
                               .target = {0, 0.5f, 0}, .fovy = 70};
        renderer_draw(renderer, &scene, &specials, &camera, true);
        uint32_t width, height;
        const uint8_t *pixels = renderer_pixels(renderer, &width, &height);
        size_t red_count = 0, green_count = 0;
        uint64_t red_position = 0, green_position = 0;
        for (size_t index = 0; index < (size_t)width * height; ++index) {
            const uint8_t *pixel = pixels + index * 4;
            if (pixel[0]) {
                if (pixel[0] != 64 || pixel[1] || pixel[2])
                    fail("Palette red 64 became %u,%u,%u during presentation", pixel[0], pixel[1], pixel[2]);
                ++red_count; red_position += index % width;
            } else if (pixel[1]) {
                expect(pixel[1] == 96 && !pixel[2], "palette green survives presentation unchanged");
                ++green_count; green_position += index % width;
            }
        }
        expect(red_count > 0 && green_count > 0, "sprite stays visible at every camera angle");
        expect((double)red_position / (double)red_count < (double)green_position / (double)green_count,
               "sprite texture keeps its left/right orientation");
        size_t count = red_count + green_count;
        if (!angle) baseline = count;
        expect(count * 10 >= baseline * 9 && count * 10 <= baseline * 11,
               "sprite keeps its projected size around the camera orbit");
    }
    RenderCamera camera = {.position = {4, 0.5f, 0}, .target = {0, 0.5f, 0}, .fovy = 70};
    renderer_draw(renderer, &scene, &specials, &camera, true);
    uint32_t width, height;
    const uint8_t *pixels = renderer_pixels(renderer, &width, &height);
    size_t bytes = (size_t)width * height * 4;
    uint8_t *reference = allocate(bytes, 1);
    memcpy(reference, pixels, bytes);
    for (unsigned tick = 0; tick < 6; ++tick) scene_tick_sprites(&scene);
    renderer_draw(renderer, &scene, &specials, &camera, false);
    pixels = renderer_pixels(renderer, &width, &height);
    expect(pixel_difference(reference, pixels, bytes) > 0.1, "sprite animation changes the GPU texture");
    expect_static(renderer, "a texture-only sprite change does not invent geometry motion");
    for (unsigned tick = 0; tick < 6; ++tick) scene_tick_sprites(&scene);
    renderer_draw(renderer, &scene, &specials, &camera, false);
    pixels = renderer_pixels(renderer, &width, &height);
    expect(!memcmp(reference, pixels, bytes), "sprite animation wraps to the same image");
    free(reference);
    renderer_destroy(renderer);
}

typedef struct {
    size_t red_count, green_count, blue_count;
    uint32_t min_x, max_x, min_y, max_y;
} SpritePixels;

static SpritePixels capture_sprite_pixels(Renderer *renderer, Scene *scene, Specials *specials,
                                           const RenderCamera *camera, const char *message) {
    renderer_draw(renderer, scene, specials, camera, true);
    expect_static(renderer, message);
    uint32_t width, height;
    const uint8_t *pixels = renderer_pixels(renderer, &width, &height);
    expect(pixels != NULL, "synthetic sprite fixture returns GPU pixels");
    SpritePixels result = {.min_x = UINT32_MAX, .min_y = UINT32_MAX};
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *pixel = pixels + ((size_t)y * width + x) * 4;
            if (pixel[0] && !pixel[1] && !pixel[2]) {
                ++result.red_count;
                if (x < result.min_x) result.min_x = x;
                if (x > result.max_x) result.max_x = x;
                if (y < result.min_y) result.min_y = y;
                if (y > result.max_y) result.max_y = y;
            } else if (!pixel[0] && pixel[1] && !pixel[2]) {
                ++result.green_count;
            } else if (!pixel[0] && !pixel[1] && pixel[2]) {
                ++result.blue_count;
            }
        }
    }
    return result;
}

static bool same_sprite_bounds(SpritePixels first, SpritePixels second) {
    return first.red_count == second.red_count && first.min_x == second.min_x &&
           first.max_x == second.max_x && first.min_y == second.min_y && first.max_y == second.max_y;
}

static void check_sprite_plane_clipping(GLFWwindow *window) {
    uint8_t frame_a[73 * 65 * 4] = {0}, frame_b[88 * 70 * 4] = {0};
    for (size_t index = 0; index < 73 * 65; ++index) {
        frame_a[index * 4] = 1;
        frame_a[index * 4 + 3] = 255;
    }
    for (size_t index = 0; index < 88 * 70; ++index) {
        frame_b[index * 4] = 1;
        frame_b[index * 4 + 3] = 255;
    }
    uint8_t ceiling_texel[] = {2, 0, 0, 255};
    uint8_t opaque_wall_texel[] = {3, 0, 0, 255};
    uint8_t transparent_wall_texel[] = {3, 0, 0, 0};
    Asset assets[] = {
        {.name = "SPRITA", .width = 73, .height = 65, .left = 39, .top = 60, .pixels = frame_a},
        {.name = "SPRITB", .width = 88, .height = 70, .left = 43, .top = 65, .pixels = frame_b},
        {.name = "CEILING", .width = 1, .height = 1, .pixels = ceiling_texel},
        {.name = "WALLOPEN", .width = 1, .height = 1, .pixels = opaque_wall_texel},
        {.name = "WALLCUT", .width = 1, .height = 1, .pixels = transparent_wall_texel}
    };
    SceneVertex ceiling[] = {
        {-4, 0, -4, 0, 0}, {4, 0, 4, 1, 1}, {4, 0, -4, 1, 0},
        {-4, 0, -4, 0, 0}, {-4, 0, 4, 0, 1}, {4, 0, 4, 1, 1}
    };
    SceneVertex wall[] = {
        {-4, 0, 1, 0, 1}, {4, 4, 1, 1, 0}, {4, 0, 1, 1, 1},
        {-4, 0, 1, 0, 1}, {-4, 4, 1, 0, 0}, {4, 4, 1, 1, 0}
    };
    Batch batches[] = {
        {.asset = 2, .owner = 0, .plane = SURFACE_CEILING, .count = 6, .vertices = ceiling},
        {.asset = 4, .plane = SURFACE_WALL, .count = 6, .vertices = wall}
    };
    SceneSector sector = {.floor = 0, .ceiling = 2, .light = 255};
    Sprite sprite = {.asset = 0, .sector = 0, .position = {0, 5.0f / 32.0f, 0},
        .fullbright = true, .frame_count = 2, .frame_ticks = 15,
        .frames = {{.asset = 0, .ticks = 15, .fullbright = true},
                   {.asset = 1, .ticks = 15, .fullbright = true}}};
    Scene scene = {.asset_count = 5, .assets = assets, .batch_count = 2, .batches = batches,
                   .sprite_count = 1, .sprites = &sprite, .sector_count = 1, .sectors = &sector};
    for (unsigned shade = 0; shade < 32; ++shade) {
        uint8_t *red = &scene.lut[(shade * 256 + 1) * 4];
        uint8_t *green = &scene.lut[(shade * 256 + 2) * 4];
        uint8_t *blue = &scene.lut[(shade * 256 + 3) * 4];
        red[0] = red[3] = 255;
        green[1] = green[3] = 255;
        blue[2] = blue[3] = 255;
    }
    Specials specials = {0};
    RenderCamera camera = {.position = {0, 1, 4}, .target = {0, 1, 0}, .fovy = 70};
    Renderer *renderer = renderer_create(window, &scene,
        (RendererOptions){.validation = true, .no_vsync = true});

    scene.batch_count = 1;
    sector.ceiling = sector.floor;
    SpritePixels frame_a_reference = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "sprite reference has valid static motion data");
    sector.ceiling = 2;
    SpritePixels frame_a_ceiling = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "ceiling comparison has valid static motion data");
    expect(frame_a_reference.red_count > 0, "synthetic frame A is visible without a ceiling");
    expect(frame_a_ceiling.green_count > 0, "synthetic ceiling is visible in green");
    expect(same_sprite_bounds(frame_a_reference, frame_a_ceiling),
           "ceiling preserves frame A red pixel bounds and count");

    scene.batch_count = 2;
    batches[1].asset = 4;
    SpritePixels frame_a_cutout_wall = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "transparent wall comparison has valid static motion data");
    expect(same_sprite_bounds(frame_a_ceiling, frame_a_cutout_wall),
           "transparent wall texels do not hide frame A");
    batches[1].asset = 3;
    SpritePixels frame_a_opaque_wall = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "opaque wall comparison has valid static motion data");
    expect(frame_a_opaque_wall.red_count == 0 && frame_a_opaque_wall.blue_count > 0,
           "opaque wall hides frame A behind the ceiling");

    for (unsigned tick = 0; tick < 15; ++tick) scene_tick_sprites(&scene);
    expect(sprite.frame_index == 1 && sprite.asset == 1, "15 ticks select Mancubus frame B");
    sector.ceiling = sector.floor;
    scene.batch_count = 1;
    SpritePixels frame_b_reference = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "frame B reference has valid static motion data");
    sector.ceiling = 2;
    SpritePixels frame_b_ceiling = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "frame B ceiling comparison has valid static motion data");
    expect(frame_b_reference.red_count > 0, "synthetic frame B is visible without a ceiling");
    expect(frame_b_ceiling.green_count > 0, "synthetic ceiling remains visible for frame B");
    expect(same_sprite_bounds(frame_b_reference, frame_b_ceiling),
           "ceiling preserves frame B red pixel bounds and count");
    expect(frame_b_reference.red_count > frame_a_reference.red_count &&
           frame_b_reference.max_x - frame_b_reference.min_x > frame_a_reference.max_x - frame_a_reference.min_x &&
           frame_b_reference.max_y - frame_b_reference.min_y > frame_a_reference.max_y - frame_a_reference.min_y &&
           frame_b_reference.min_y < frame_a_reference.min_y,
           "frame B grows and extends above frame A");

    scene.batch_count = 2;
    batches[1].asset = 4;
    SpritePixels frame_b_cutout_wall = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "frame B transparent wall comparison has valid static motion data");
    expect(same_sprite_bounds(frame_b_ceiling, frame_b_cutout_wall),
           "transparent wall texels do not hide frame B");
    batches[1].asset = 3;
    SpritePixels frame_b_opaque_wall = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "frame B opaque wall comparison has valid static motion data");
    expect(frame_b_opaque_wall.red_count == 0 && frame_b_opaque_wall.blue_count > 0,
           "opaque wall hides frame B behind the ceiling");

    renderer_destroy(renderer);
    scene.batch_count = 1;
    sector.ceiling = 65.0f / 32;
    sprite.asset = sprite.frame_index = 0;
    sprite.frame_ticks = 15;
    renderer = renderer_create(window, &scene, (RendererOptions){.validation = true, .no_vsync = true});
    for (unsigned phase = 0; phase < 4; ++phase) {
        SpritePixels pixels = capture_sprite_pixels(renderer, &scene, &specials, &camera,
            "switching sprite depth paths preserves static motion data");
        expect(same_sprite_bounds(pixels, sprite.frame_index ? frame_b_reference : frame_a_reference),
               "animation can enter and leave ceiling overflow without clipping");
        for (unsigned tick = 0; tick < 15; ++tick) scene_tick_sprites(&scene);
    }
    renderer_destroy(renderer);

    SceneSector regression_sectors[] = {
        {.floor = 0, .ceiling = 2, .light = 255},
        {.floor = -4, .ceiling = 2, .light = 255}
    };
    Sprite regression_sprites[] = {
        {.asset = 0, .sector = 1, .position = {0, -2.5f, 0}, .fullbright = true},
        {.asset = 1, .sector = 0, .position = {50, 5.0f / 32.0f, 0}, .fullbright = true, .hidden = true}
    };
    scene.sectors = regression_sectors;
    scene.sector_count = 2;
    scene.sprites = regression_sprites;
    scene.sprite_count = 2;
    scene.batch_count = 1;
    batches[0].plane = SURFACE_FLOOR;
    renderer = renderer_create(window, &scene, (RendererOptions){.validation = true, .no_vsync = true});
    SpritePixels hidden_sprite = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "normal sprite behind floor has valid static motion data");
    expect(hidden_sprite.red_count == 0 && hidden_sprite.green_count > 0,
           "floor hides a normal sprite within its own sector planes");
    regression_sprites[1].hidden = false;
    SpritePixels hidden_with_overflow = capture_sprite_pixels(renderer, &scene, &specials, &camera,
        "offscreen overflow comparison has valid static motion data");
    expect(hidden_with_overflow.red_count == 0,
           "offscreen overflowing sprite does not reveal a normal sprite behind the floor");
    renderer_destroy(renderer);
}

static unsigned shadow_sample(Renderer *renderer, Scene *scene, Specials *specials, const RenderCamera *camera) {
    renderer_draw(renderer, scene, specials, camera, false);
    uint32_t width, height;
    const uint8_t *pixels = renderer_pixels(renderer, &width, &height);
    uint32_t column = (uint32_t)lroundf((float)width * 0.5f + (float)height * 0.5f /
                                      (16.0f * tanf(35.0f * 3.14159265358979323846f / 180.0f)));
    return pixels[((size_t)(height / 2) * width + column) * 4];
}

static void check_ray_shadows(GLFWwindow *window) {
    uint8_t opaque[] = {1, 0, 0, 255}, transparent[] = {1, 0, 0, 0};
    uint8_t sprite_texels[128 * 96 * 4] = {0}, sprite_cutout[128 * 96 * 4] = {0};
    for (size_t index = 0; index < 128 * 96; ++index) {
        sprite_texels[index * 4] = 1;
        sprite_texels[index * 4 + 3] = 255;
    }
    Asset assets[] = {
        {.name = "FLOOR", .width = 1, .height = 1, .pixels = opaque},
        {.name = "WALL", .width = 1, .height = 1, .pixels = opaque},
        {.name = "CUTOUT", .width = 1, .height = 1, .pixels = transparent},
        {.name = "MOB", .width = 128, .height = 96, .left = 64, .top = 96, .pixels = sprite_texels},
        {.name = "MOB2", .width = 128, .height = 96, .left = 64, .top = 96, .pixels = sprite_cutout}};
    SceneVertex floor[] = {{-4, 0, -4, 0, 0}, {4, 0, 4, 1, 1}, {4, 0, -4, 1, 0},
                          {-4, 0, -4, 0, 0}, {-4, 0, 4, 0, 1}, {4, 0, 4, 1, 1}};
    SceneVertex wall[] = {{0, 0, -2, 0, 0}, {0, 3, 2, 1, 1}, {0, 0, 2, 1, 0},
                         {0, 0, -2, 0, 0}, {0, 3, -2, 0, 1}, {0, 3, 2, 1, 1}};
    Batch batches[] = {{.asset = 0, .plane = SURFACE_FLOOR, .count = 6, .vertices = floor},
                       {.asset = 1, .plane = SURFACE_WALL, .count = 6, .vertices = wall}};
    SceneSector sector = {.floor = 0, .ceiling = 4, .light = 255};
    Scene scene = {.map = "RAYTEST", .asset_count = 5, .assets = assets, .batch_count = 2,
        .batches = batches, .sector_count = 1, .sectors = &sector};
    for (unsigned shade = 0; shade < 32; ++shade) {
        uint8_t *color = &scene.lut[(shade * 256 + 1) * 4];
        color[0] = 128; color[1] = 128; color[2] = 128; color[3] = 255;
    }
    Specials specials = specials_create(&scene);
    RenderCamera camera = {.position = {0, 8, 0}, .target = {0, 0, 0}, .fovy = 70};
    RendererOptions options = {.validation = true, .ray_lighting = true, .no_vsync = true};
    Renderer *renderer = renderer_create(window, &scene, options);
    unsigned shadowed = shadow_sample(renderer, &scene, &specials, &camera);
    for (unsigned vertex = 0; vertex < 6; ++vertex) wall[vertex].x = 10;
    unsigned moved = shadow_sample(renderer, &scene, &specials, &camera);
    expect(moved > shadowed + 20, "moving an occluder updates its ray-traced shadow");
    expect(renderer_set_ray_lighting(renderer, false), "ray lighting can be disabled");
    unsigned classic = shadow_sample(renderer, &scene, &specials, &camera);
    expect(classic == 128, "ray toggle restores the exact classic palette value");
    expect(renderer_set_ray_lighting(renderer, true), "ray lighting can be enabled");
    for (unsigned vertex = 0; vertex < 6; ++vertex) wall[vertex].x = 0;
    unsigned restored = shadow_sample(renderer, &scene, &specials, &camera);
    expect(abs((int)restored - (int)shadowed) <= 2, "restoring an occluder restores its shadow");
    renderer_destroy(renderer);
    batches[1].asset = 2;
    renderer = renderer_create(window, &scene, options);
    unsigned cutout = shadow_sample(renderer, &scene, &specials, &camera);
    expect(cutout > shadowed + 20, "transparent wall texels do not block shadow rays");
    printf("Ray shadow samples: solid=%u moved=%u restored=%u transparent=%u classic=%u\n",
           shadowed, moved, restored, cutout, classic);
    renderer_destroy(renderer);
    Sprite sprite = {.asset = 3, .frame_count = 2,
        .frames = {{.asset = 3, .ticks = 1}, {.asset = 4, .ticks = 1}}};
    scene.sprites = &sprite;
    scene.sprite_count = 1;
    scene.batch_count = 1;
    renderer = renderer_create(window, &scene, options);
    unsigned sprite_shadow = shadow_sample(renderer, &scene, &specials, &camera);
    sprite.position.x = 10;
    unsigned sprite_moved = shadow_sample(renderer, &scene, &specials, &camera);
    expect(sprite_moved > sprite_shadow + 20, "moving a sprite updates its ray shadow");
    sprite.position.x = 0;
    sprite.hidden = true;
    unsigned sprite_hidden = shadow_sample(renderer, &scene, &specials, &camera);
    expect(abs((int)sprite_hidden - (int)sprite_moved) <= 2, "hidden sprites cast no ray shadow");
    sprite.hidden = false;
    sprite.asset = 4;
    unsigned sprite_transparent = shadow_sample(renderer, &scene, &specials, &camera);
    expect(abs((int)sprite_transparent - (int)sprite_moved) <= 2, "animated sprite alpha controls ray shadows");
    renderer_destroy(renderer);
    SceneVertex voxel_vertices[18];
    for (unsigned vertex = 0; vertex < 18; ++vertex) {
        voxel_vertices[vertex] = vertex < 12 ? wall[vertex % 6] : floor[vertex - 12];
        if (vertex >= 6 && vertex < 12) voxel_vertices[vertex].x -= 0.5f;
        voxel_vertices[vertex].u = voxel_vertices[vertex].v = 0.5f;
    }
    assets[4].voxel = (VoxelMesh){.count = 18, .vertices = voxel_vertices};
    assets[3].voxel = (VoxelMesh){.count = 6, .vertices = voxel_vertices};
    scene.voxel_asset = 0;
    sprite.position.y = -10;
    renderer = renderer_create(window, &scene, options);
    unsigned model_shadow = shadow_sample(renderer, &scene, &specials, &camera);
    shadow_sample(renderer, &scene, &specials, &camera);
    expect_static(renderer, "cached static voxel mesh has no stale motion");
    sprite.position.x = 0.25f;
    shadow_sample(renderer, &scene, &specials, &camera);
    expect_motion(renderer, "moving a voxel mesh produces object motion");
    shadow_sample(renderer, &scene, &specials, &camera);
    expect_static(renderer, "stopping a voxel mesh clears motion on the next frame");
    shadow_sample(renderer, &scene, &specials, &camera);
    expect_static(renderer, "cached stopped voxel mesh keeps zero motion");
    sprite.position.x = 10;
    unsigned model_moved = shadow_sample(renderer, &scene, &specials, &camera);
    expect(model_moved > model_shadow + 20,
        "voxel meshes self-shadow with opaque materials and floor anchors, not sprite alpha or baseline");
    sprite.position.x = 0;
    sprite.hidden = true;
    unsigned model_hidden = shadow_sample(renderer, &scene, &specials, &camera);
    expect(abs((int)model_hidden - (int)model_moved) <= 2, "hidden voxel meshes cast no shadow");
    sprite.hidden = false;
    sprite.asset = 3;
    unsigned smaller_model = shadow_sample(renderer, &scene, &specials, &camera);
    expect(smaller_model + 20 < model_moved, "smaller animated meshes select their shared pose");
    for (unsigned phase = 0; phase < 12; ++phase) {
        sprite.asset = phase % 2 ? 3 : 4;
        sprite.hidden = phase % 3 == 0;
        unsigned sample = shadow_sample(renderer, &scene, &specials, &camera);
        expect(sprite.hidden ? abs((int)sample - (int)model_moved) <= 2 : sample + 20 < model_moved,
            "shared pose and hidden-instance changes keep correct shadows across repeated cycles");
    }
    renderer_destroy(renderer);
    assets[3].voxel = (VoxelMesh){0};
    assets[4].voxel = (VoxelMesh){0};
    sprite.position.y = 0;
    sprite.asset = 4;
    uint8_t lamp_texel[] = {2, 0, 0, 255};
    assets[4] = (Asset){.name = "TLMPA0", .width = 1, .height = 1, .top = 48, .pixels = lamp_texel};
    for (unsigned shade = 0; shade < 32; ++shade) {
        uint8_t *blue = &scene.lut[(shade * 256 + 2) * 4];
        blue[2] = 255; blue[3] = 255;
    }
    sprite.fullbright = true;
    renderer = renderer_create(window, &scene, options);
    shadow_sample(renderer, &scene, &specials, &camera);
    uint32_t width, height;
    const uint8_t *pixels = renderer_pixels(renderer, &width, &height);
    uint32_t column = (uint32_t)lroundf((float)width * 0.5f + (float)height * 0.5f /
                                      (16.0f * tanf(35.0f * 3.14159265358979323846f / 180.0f)));
    size_t sample = ((size_t)(height / 2) * width + column) * 4;
    expect(pixels[sample + 2] > pixels[sample] + 8, "sprite light casts its palette color onto the floor");
    unsigned lit_blue = pixels[sample + 2];
    sprite.hidden = true;
    shadow_sample(renderer, &scene, &specials, &camera);
    pixels = renderer_pixels(renderer, &width, &height);
    expect(lit_blue > pixels[sample + 2] + 8, "hiding an emitter removes its colored light");
    renderer_destroy(renderer);
    specials_free(&specials);
}

static void check_reconstruction(GLFWwindow *window, Scene *scene, Specials *specials, bool frame_generation) {
    RenderCamera camera = {.position = scene->spawn,
        .target = {scene->spawn.x + cosf(scene->yaw), scene->spawn.y, scene->spawn.z - sinf(scene->yaw)}, .fovy = 70};
    Renderer *renderer = renderer_create(window, scene, (RendererOptions){.validation = true,
        .dlss_enabled = true, .frame_generation = frame_generation, .ray_lighting = true,
        .ray_reconstruction = true, .upscale_mode = 3, .no_vsync = true});
    renderer_set_neural(renderer, false);
    for (unsigned frame = 0; frame < 16; ++frame) renderer_draw(renderer, scene, specials, &camera, frame == 0);
    uint32_t width, height;
    const uint8_t *pixels = renderer_pixels(renderer, &width, &height);
    size_t bytes = (size_t)width * height * 4;
    uint8_t *reference = allocate(bytes, 1);
    memcpy(reference, pixels, bytes);
    expect(renderer_set_ray_reconstruction(renderer, false), "RR can be disabled live");
    for (unsigned frame = 0; frame < 16; ++frame) renderer_draw(renderer, scene, specials, &camera, false);
    pixels = renderer_pixels(renderer, &width, &height);
    double difference = pixel_difference(reference, pixels, bytes);
    expect(difference > 0.01, "RR and unreconstructed lighting produce different GPU images");
    free(reference);
    expect(renderer_set_ray_reconstruction(renderer, true), "RR can resume live");
    for (unsigned mode = 0; mode <= 3; ++mode) {
        expect(renderer_set_upscale_mode(renderer, mode), "RR supports DLAA and all SR quality modes");
        renderer_draw(renderer, scene, specials, &camera, true);
        expect_static(renderer, "RR quality change resets motion history");
        for (unsigned frame = 1; frame < 32; ++frame) renderer_draw(renderer, scene, specials, &camera, false);
        pixels = renderer_pixels(renderer, &width, &height);
        bytes = (size_t)width * height * 4;
        reference = allocate(bytes, 1);
        memcpy(reference, pixels, bytes);
        renderer_set_neural(renderer, true);
        renderer_set_neural_passes(renderer, 2);
        for (unsigned frame = 0; frame < 32; ++frame) renderer_draw(renderer, scene, specials, &camera, frame == 0);
        pixels = renderer_pixels(renderer, &width, &height);
        double preference_difference = pixel_difference(reference, pixels, bytes);
        printf("RR mode %u: NR preference replay difference %.3f\n", mode, preference_difference);
        expect(preference_difference < 1.0, "NR is not stacked onto the RR mode");
        free(reference);
        camera.position.x += 0.1f; camera.target.x += 0.1f;
        renderer_draw(renderer, scene, specials, &camera, false);
        expect_motion(renderer, "RR camera motion is preserved for reconstruction and FG");
        renderer_set_neural(renderer, false);
    }
    renderer_set_ray_lighting(renderer, false);
    renderer_draw(renderer, scene, specials, &camera, false);
    renderer_set_ray_lighting(renderer, true);
    renderer_set_ray_reconstruction(renderer, false);
    renderer_set_neural(renderer, true);
    renderer_draw(renderer, scene, specials, &camera, true);
    renderer_set_ray_reconstruction(renderer, true);
    glfwSetWindowSize(window, 800, 600);
    glfwPollEvents();
    renderer_draw(renderer, scene, specials, &camera, false);
    pixels = renderer_pixels(renderer, &width, &height);
    expect(pixels && width == 800 && height == 600, "RR output follows window resize");
    printf("Ray Reconstruction comparison: %.3f average pixel difference\n", difference);
    renderer_destroy(renderer);
}

static int compare_times(const void *first, const void *second) {
    double left = *(const double *)first, right = *(const double *)second;
    return (left > right) - (left < right);
}

static void benchmark_motion(GLFWwindow *window, Scene *scene, Specials *specials, bool ray_lighting,
                             unsigned frames, bool validation) {
    Renderer *renderer = renderer_create(window, scene,
        (RendererOptions){.ray_lighting = ray_lighting, .no_vsync = true, .validation = validation});
    renderer_enable_profile(renderer);
    unsigned count = frames - 16;
    double *samples = allocate(count, sizeof(*samples)), total = 0;
    RenderCamera camera = {.position = scene->spawn, .fovy = 70};
    for (unsigned frame = 0; frame < frames; ++frame) {
        if (frame % 4 == 0) scene_tick_sprites(scene);
        float yaw = scene->yaw + sinf((float)frame * 0.04f) * 0.4f;
        camera.position.x = scene->spawn.x + sinf((float)frame * 0.02f) * 0.2f;
        camera.target = (Position){camera.position.x + cosf(yaw), camera.position.y,
            camera.position.z - sinf(yaw)};
        double start = glfwGetTime();
        renderer_draw(renderer, scene, specials, &camera, frame == 0);
        if (frame >= 16) {
            double elapsed = (glfwGetTime() - start) * 1000;
            samples[frame - 16] = elapsed;
            total += elapsed;
        }
    }
    qsort(samples, count, sizeof(*samples), compare_times);
    printf("Dynamic %s frames=%u ray=%u: mean=%.3fms p95=%.3fms max=%.3fms\n", scene->map,
        frames, (unsigned)ray_lighting, total / count, samples[count * 95 / 100], samples[count - 1]);
    free(samples);
    renderer_destroy(renderer);
}

int main(int argc, char **argv) {
    if (argc < 2) fail("Usage: check-renderer SCENE.drmap [--voxels] [--benchmark [FRAMES]] [--validation] [--dlss5] [--fg] [--ray-lighting]");
    bool neural = false, frame_generation = false, ray_lighting = false, reconstruction = false;
    bool benchmark = false, voxels = false;
    bool benchmark_validation = false;
    unsigned benchmark_frames = 128;
    for (int index = 2; index < argc; ++index) {
        if (!strcmp(argv[index], "--dlss5")) neural = true;
        else if (!strcmp(argv[index], "--fg")) frame_generation = true;
        else if (!strcmp(argv[index], "--ray-lighting")) ray_lighting = true;
        else if (!strcmp(argv[index], "--rr")) reconstruction = true;
        else if (!strcmp(argv[index], "--benchmark")) {
            benchmark = true;
            if (index + 1 < argc && argv[index + 1][0] >= '0' && argv[index + 1][0] <= '9') {
                char *end;
                unsigned long frames = strtoul(argv[++index], &end, 10);
                if (*end || frames < 17 || frames > 4096) fail("Benchmark frames must be 17..4096");
                benchmark_frames = (unsigned)frames;
            }
        }
        else if (!strcmp(argv[index], "--validation")) benchmark_validation = true;
        else if (!strcmp(argv[index], "--voxels")) voxels = true;
        else fail("Unknown renderer test option: %s", argv[index]);
    }
    expect(!frame_generation || neural, "FG test requires neural mode");
    expect(!benchmark || (!neural && !frame_generation && !reconstruction), "motion benchmark is native-only");

    Scene scene = scene_read(argv[1]);
    if (voxels) {
        char *path = scene_sidecar(argv[1], ".drvox");
        scene_read_voxels(&scene, path);
        free(path);
    }
    expect(benchmark || strcmp(scene.map, "MAP01") == 0, "input scene is MAP01");
    Specials specials = specials_create(&scene);

    expect(glfwInit() == GLFW_TRUE, "GLFW initializes");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *window = glfwCreateWindow(640, 360, "renderer-check", NULL, NULL);
    expect(window != NULL, "hidden Vulkan window is created");

    if (benchmark) {
        benchmark_motion(window, &scene, &specials, ray_lighting, benchmark_frames, benchmark_validation);
        glfwDestroyWindow(window); glfwTerminate();
        specials_free(&specials); scene_free(&scene);
        return 0;
    }

    if (reconstruction) {
        check_reconstruction(window, &scene, &specials, frame_generation);
        glfwDestroyWindow(window);
        glfwTerminate();
        specials_free(&specials);
        scene_free(&scene);
        puts("Passed separate DLSS RR/NR modes, motion, quality, and resize checks");
        return 0;
    }

    Renderer *renderer = renderer_create(window, &scene, (RendererOptions){.validation = true,
        .dlss_enabled = neural, .frame_generation = frame_generation, .ray_lighting = ray_lighting,
        .no_vsync = !frame_generation});
    RenderCamera camera = {
        .position = scene.spawn,
        .target = {scene.spawn.x + cosf(scene.yaw), scene.spawn.y, scene.spawn.z - sinf(scene.yaw)},
        .fovy = 70
    };

    renderer_text(renderer, "Vulkan HUD | Health 100 | Secrets 0/5", 12, 8);
    renderer_draw(renderer, &scene, &specials, &camera, true);
    expect_static(renderer, "first frame resets motion history");
    renderer_draw(renderer, &scene, &specials, &camera, false);
    expect_static(renderer, "repeated static frame has no motion");
    if (neural) {
        uint32_t width, height;
        const uint8_t *pixels = renderer_pixels(renderer, &width, &height);
        size_t bytes = (size_t)width * height * 4;
        uint8_t *reference = allocate(bytes, 1);
        memcpy(reference, pixels, bytes);
        expect(renderer_set_neural(renderer, false), "neural mode can be disabled");
        renderer_draw(renderer, &scene, &specials, &camera, false);
        pixels = renderer_pixels(renderer, &width, &height);
        expect(pixel_difference(pixels, reference, bytes) > 1.0, "neural output has a measurable image effect");
        memcpy(reference, pixels, bytes);
        expect(renderer_set_neural(renderer, true), "neural mode can be enabled again");
        renderer_draw(renderer, &scene, &specials, &camera, false);
        expect_static(renderer, "neural toggle resets motion history");
        pixels = renderer_pixels(renderer, &width, &height);
        expect(pixel_difference(pixels, reference, bytes) > 1.0, "neural output returns after toggling");
        expect(!renderer_set_neural_style(renderer, 3), "invalid neural style is rejected");
        for (unsigned style = 0; style < 3; ++style) {
            expect(renderer_set_neural_style(renderer, style), "neural style can change");
            renderer_draw(renderer, &scene, &specials, &camera, false);
            expect_static(renderer, "style change resets motion history");
            pixels = renderer_pixels(renderer, &width, &height);
            expect(pixel_difference(pixels, reference, bytes) > 1.0, "every neural style changes the image");
        }
        renderer_set_neural_style(renderer, 0);
        expect(!renderer_set_neural_passes(renderer, 0) && !renderer_set_neural_passes(renderer, 5),
               "invalid neural pass counts are rejected");
        for (unsigned passes = 1; passes <= 4; ++passes) {
            expect(renderer_set_neural_passes(renderer, passes), "neural pass count can change");
            renderer_draw(renderer, &scene, &specials, &camera, false);
            expect_static(renderer, "pass change resets motion history");
            pixels = renderer_pixels(renderer, &width, &height);
            expect(pixel_difference(pixels, reference, bytes) > 1.0, "each pass count produces neural output");
            renderer_draw(renderer, &scene, &specials, &camera, false);
            expect_static(renderer, "multiple passes keep a static motion buffer");
        }
        renderer_set_neural_passes(renderer, 2);
        renderer_draw(renderer, &scene, &specials, &camera, false);
        expect_static(renderer, "two-pass history is primed before camera movement");
        free(reference);
    } else {
        expect(!renderer_set_neural(renderer, true), "native-only renderer does not claim neural support");
    }

    Position direction = {camera.target.x - camera.position.x,
                          camera.target.y - camera.position.y,
                          camera.target.z - camera.position.z};
    camera.position.x += direction.x * 0.75f;
    camera.position.y += direction.y * 0.75f;
    camera.position.z += direction.z * 0.75f;
    camera.target.x += direction.x * 0.75f;
    camera.target.y += direction.y * 0.75f;
    camera.target.z += direction.z * 0.75f;
    renderer_draw(renderer, &scene, &specials, &camera, false);
    expect_motion(renderer, "camera movement produces GPU motion");
    renderer_draw(renderer, &scene, &specials, &camera, true);
    expect_static(renderer, "camera history reset clears motion");
    renderer_draw(renderer, &scene, &specials, &camera, false);
    expect_static(renderer, "static frame after camera reset has no motion");

    expect(scene.lines[84].special == 102 && scene.lines[84].tag == 3,
           "MAP01 barrier switch is present");
    float yaw;
    Player barrier_user = switch_player(&scene, &scene.lines[84], &yaw);
    camera = camera_at(&barrier_user, yaw);
    renderer_draw(renderer, &scene, &specials, &camera, true);
    expect_static(renderer, "barrier view reset has no motion");
    expect(specials_use(&scene, &specials, &barrier_user, yaw), "MAP01 barrier switch activates");
    float barrier_floor = scene.sectors[46].floor;
    specials_tick(&scene, &specials, NULL);
    expect(scene.sectors[46].floor < barrier_floor, "MAP01 barrier sector starts moving");
    renderer_draw(renderer, &scene, &specials, &camera, false);
    expect_motion(renderer, "moving MAP01 barrier produces GPU motion");
    for (unsigned tick = 1; tick < 64; ++tick) specials_tick(&scene, &specials, NULL);

    expect(scene.lines[126].special == 62 && scene.lines[126].tag == 5,
           "MAP01 lift switch is present");
    Player lift_user = switch_player(&scene, &scene.lines[126], &yaw);
    camera = camera_at(&lift_user, yaw);
    renderer_draw(renderer, &scene, &specials, &camera, true);
    expect_static(renderer, "lift view reset has no motion");
    expect(specials_use(&scene, &specials, &lift_user, yaw), "MAP01 lift switch activates");
    float lift_floor = scene.sectors[21].floor;
    specials_tick(&scene, &specials, NULL);
    expect(scene.sectors[21].floor < lift_floor, "MAP01 lift floor starts moving");
    renderer_draw(renderer, &scene, &specials, &camera, false);
    expect_motion(renderer, "moving MAP01 lift produces GPU motion");
    renderer_draw(renderer, &scene, &specials, &camera, false);
    expect_static(renderer, "stationary lift frame has no motion");

    if (neural) renderer_set_neural(renderer, false);
    glfwSetWindowSize(window, 800, 600);
    glfwPollEvents();
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    expect(width > 640 && height > 360, "resized Vulkan window has a larger framebuffer");
    renderer_draw(renderer, &scene, &specials, &camera, true);
    expect_static(renderer, "resized renderer rebuilds with reset motion history");
    if (neural) {
        renderer_set_neural(renderer, true);
        renderer_draw(renderer, &scene, &specials, &camera, false);
        expect_static(renderer, "neural rendering resumes after a native-mode resize");
    }
    renderer_text(renderer, "Resized HUD", 12, 8);
    renderer_draw(renderer, &scene, &specials, &camera, false);
    expect_static(renderer, "HUD does not write into motion history");

    if (neural) {
        for (unsigned combination = 0; combination < 8; ++combination) {
            bool nr_on = (combination & 1u) != 0;
            bool sr_on = (combination & 2u) != 0;
            bool fg_on = (combination & 4u) != 0;
            expect(renderer_set_neural(renderer, nr_on), "live NR toggle succeeds");
            expect(renderer_set_upscale_mode(renderer, sr_on ? 3u : 0u), "live SR toggle succeeds");
            if (frame_generation)
                expect(renderer_set_frame_generation(renderer, fg_on), "live FG toggle succeeds");
            if (ray_lighting)
                expect(renderer_set_ray_lighting(renderer, nr_on), "live ray lighting toggle succeeds");
            renderer_draw(renderer, &scene, &specials, &camera, false);
            expect_static(renderer, "live feature change resets motion history");
            renderer_draw(renderer, &scene, &specials, &camera, false);
            uint32_t toggle_width, toggle_height;
            expect(renderer_pixels(renderer, &toggle_width, &toggle_height) != NULL,
                   "live feature combination produces a frame");
            if (frame_generation && !fg_on)
                expect(renderer_generated_pixels(renderer, &toggle_width, &toggle_height) == NULL,
                       "FG off does not expose a stale generated frame");
        }
    }

    renderer_destroy(renderer);
    size_t sky_index = 0;
    while (sky_index < scene.batch_count && !scene.batches[sky_index].sky) ++sky_index;
    expect(sky_index < scene.batch_count, "MAP01 has a sky texture");
    Batch sky_batch = scene.batches[sky_index];
    sky_batch.count = 0;
    sky_batch.plane = SURFACE_WALL;
    Scene sky_scene = scene;
    sky_scene.batches = &sky_batch;
    sky_scene.batch_count = 1;
    sky_scene.wall_count = 0;
    sky_scene.sprite_count = 0;
    renderer = renderer_create(window, &sky_scene, (RendererOptions){.validation = true,
        .dlss_enabled = neural, .frame_generation = frame_generation, .ray_lighting = ray_lighting, .no_vsync = true});
    camera = (RenderCamera){.position = {0, 0, 0}, .target = {1, 0, 0}, .fovy = 70};
    renderer_draw(renderer, &sky_scene, &specials, &camera, true);
    expect_static(renderer, "sky history starts at zero");
    camera.position.x += 3;
    camera.target.x += 3;
    renderer_draw(renderer, &sky_scene, &specials, &camera, false);
    expect_static(renderer, "sky ignores camera translation");
    camera.target = (Position){camera.position.x + cosf(0.2f), 0, -sinf(0.2f)};
    renderer_draw(renderer, &sky_scene, &specials, &camera, false);
    expect_motion(renderer, "sky follows camera rotation");
    renderer_draw(renderer, &sky_scene, &specials, &camera, true);
    expect_static(renderer, "sky rotation history resets");
    renderer_destroy(renderer);
    check_billboards(window);
    check_sprite_plane_clipping(window);
    if (neural) {
        for (unsigned mode = 1; mode <= 3; ++mode) {
            glfwSetWindowSize(window, 640, 360);
            glfwPollEvents();
            renderer = renderer_create(window, &scene, (RendererOptions){.validation = true,
                .dlss_enabled = true, .upscale_mode = mode, .frame_generation = frame_generation,
                .ray_lighting = ray_lighting, .no_vsync = true});
            camera = (RenderCamera){
                .position = scene.spawn,
                .target = {scene.spawn.x + cosf(scene.yaw), scene.spawn.y, scene.spawn.z - sinf(scene.yaw)},
                .fovy = 70
            };
            for (unsigned frame = 0; frame < 32; ++frame)
                renderer_draw(renderer, &scene, &specials, &camera, frame == 0);
            uint32_t output_width, output_height;
            const uint8_t *pixels = renderer_pixels(renderer, &output_width, &output_height);
            glfwGetFramebufferSize(window, &width, &height);
            expect(pixels && output_width == (uint32_t)width && output_height == (uint32_t)height,
                   "SR output matches display size");
            size_t bytes = (size_t)output_width * output_height * 4;
            uint8_t *reference = allocate(bytes, 1);
            memcpy(reference, pixels, bytes);
            for (unsigned frame = 0; frame < 32; ++frame)
                renderer_draw(renderer, &scene, &specials, &camera, false);
            pixels = renderer_pixels(renderer, &output_width, &output_height);
            double static_difference = pixel_difference(pixels, reference, bytes);
            RenderCamera start_camera = camera;
            for (unsigned frame = 0; frame < 32; ++frame) {
                camera.position.x += 0.02f;
                camera.target.x += 0.02f;
                renderer_draw(renderer, &scene, &specials, &camera, false);
            }
            expect_motion(renderer, "SR camera motion remains valid");
            if (frame_generation) {
                pixels = renderer_generated_pixels(renderer, &output_width, &output_height);
                expect(pixels && output_width == 640 && output_height == 360,
                       "FG exposes a generated display-sized frame after camera movement");
                uint8_t *generated = allocate(bytes, 1);
                memcpy(generated, pixels, bytes);
                pixels = renderer_pixels(renderer, &output_width, &output_height);
                double generated_difference = pixel_difference(pixels, generated, bytes);
                printf("SR mode %u: generated/real difference %.3f\n", mode, generated_difference);
                expect(generated_difference > 0.01, "FG output is not a copy of the real moving frame");
                free(generated);
            }
            camera = start_camera;
            for (unsigned frame = 0; frame < 32; ++frame)
                renderer_draw(renderer, &scene, &specials, &camera, frame == 0);
            pixels = renderer_pixels(renderer, &output_width, &output_height);
            double reset_difference = pixel_difference(pixels, reference, bytes);
            printf("SR mode %u: static difference %.3f, reset replay difference %.3f\n",
                   mode, static_difference, reset_difference);
            expect(reset_difference < 1.0, "SR camera cut clears prior view history");
            free(reference);
            expect(renderer_set_neural(renderer, false), "SR supports disabling NR");
            glfwSetWindowSize(window, 800, 600);
            glfwPollEvents();
            renderer_draw(renderer, &scene, &specials, &camera, true);
            pixels = renderer_pixels(renderer, &output_width, &output_height);
            glfwGetFramebufferSize(window, &width, &height);
            expect(pixels && output_width == (uint32_t)width && output_height == (uint32_t)height,
                   "SR-only output follows resize");
            expect(renderer_set_neural(renderer, true), "NR resumes with SR after resize");
            camera.position.x += 0.1f;
            camera.target.x += 0.1f;
            renderer_draw(renderer, &scene, &specials, &camera, false);
            renderer_destroy(renderer);
        }
    }
    if (ray_lighting) check_ray_shadows(window);
    glfwDestroyWindow(window);
    glfwTerminate();
    specials_free(&specials);
    scene_free(&scene);
    puts("Passed GPU frame history, camera motion, MAP01 movers, sky, and resize checks");
    return 0;
}
