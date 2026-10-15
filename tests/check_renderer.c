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
    Asset assets[] = {
        {.name = "FLOOR", .width = 1, .height = 1, .pixels = opaque},
        {.name = "WALL", .width = 1, .height = 1, .pixels = opaque},
        {.name = "CUTOUT", .width = 1, .height = 1, .pixels = transparent}};
    SceneVertex floor[] = {{-4, 0, -4, 0, 0}, {4, 0, 4, 1, 1}, {4, 0, -4, 1, 0},
                          {-4, 0, -4, 0, 0}, {-4, 0, 4, 0, 1}, {4, 0, 4, 1, 1}};
    SceneVertex wall[] = {{0, 0, -2, 0, 0}, {0, 3, 2, 1, 1}, {0, 0, 2, 1, 0},
                         {0, 0, -2, 0, 0}, {0, 3, -2, 0, 1}, {0, 3, 2, 1, 1}};
    Batch batches[] = {{.asset = 0, .plane = SURFACE_FLOOR, .count = 6, .vertices = floor},
                       {.asset = 1, .plane = SURFACE_WALL, .count = 6, .vertices = wall}};
    SceneSector sector = {.floor = 0, .ceiling = 4, .light = 255};
    Scene scene = {.map = "RAYTEST", .asset_count = 3, .assets = assets, .batch_count = 2,
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
    specials_free(&specials);
}

int main(int argc, char **argv) {
    if (argc < 2) fail("Usage: check-renderer MAP01.drmap [--dlss5] [--fg] [--ray-lighting]");
    bool neural = false, frame_generation = false, ray_lighting = false;
    for (int index = 2; index < argc; ++index) {
        if (!strcmp(argv[index], "--dlss5")) neural = true;
        else if (!strcmp(argv[index], "--fg")) frame_generation = true;
        else if (!strcmp(argv[index], "--ray-lighting")) ray_lighting = true;
        else fail("Unknown renderer test option: %s", argv[index]);
    }
    expect(!frame_generation || neural, "FG test requires neural mode");

    Scene scene = scene_read(argv[1]);
    expect(strcmp(scene.map, "MAP01") == 0, "input scene is MAP01");
    Specials specials = specials_create(&scene);

    expect(glfwInit() == GLFW_TRUE, "GLFW initializes");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *window = glfwCreateWindow(640, 360, "renderer-check", NULL, NULL);
    expect(window != NULL, "hidden Vulkan window is created");

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
