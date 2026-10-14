#include "scene.h"
#include "movement.h"
#include "music.h"
#include "specials.h"

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char vertex_shader[] = {
#embed "shaders/world.vs" suffix(, 0)
};
static const char fragment_shader[] = {
#embed "shaders/world.fs" suffix(, 0)
};

static Mesh upload_vertices(const SceneVertex *vertices, size_t count, unsigned shade, bool sky) {
    Mesh mesh = {.vertexCount = (int)count, .triangleCount = (int)(count / 3)};
    mesh.vertices = MemAlloc((unsigned int)(count * 3 * sizeof(float)));
    mesh.texcoords = MemAlloc((unsigned int)(count * 2 * sizeof(float)));
    mesh.colors = MemAlloc((unsigned int)(count * 4));
    if (!mesh.vertices || !mesh.texcoords || !mesh.colors) fail("Out of GPU staging memory");
    for (size_t index = 0; index < count; ++index) {
        mesh.vertices[index * 3] = vertices[index].x;
        mesh.vertices[index * 3 + 1] = vertices[index].y;
        mesh.vertices[index * 3 + 2] = vertices[index].z;
        mesh.texcoords[index * 2] = vertices[index].u;
        mesh.texcoords[index * 2 + 1] = vertices[index].v;
        mesh.colors[index * 4] = (unsigned char)shade;
        mesh.colors[index * 4 + 1] = sky ? 255 : 0;
        mesh.colors[index * 4 + 2] = 0;
        mesh.colors[index * 4 + 3] = 255;
    }
    UploadMesh(&mesh, false);
    if (!mesh.vaoId) fail("Mesh upload failed");
    return mesh;
}

static Mesh sprite_mesh(const Asset *asset, unsigned shade) {
    float left = -(float)asset->left / 32;
    float right = ((float)asset->width - (float)asset->left) / 32;
    float top = (float)asset->top / 32;
    float bottom = ((float)asset->top - (float)asset->height) / 32;
    SceneVertex vertices[6] = {
        {left, bottom, 0, 0, 1}, {right, bottom, 0, 1, 1}, {right, top, 0, 1, 0},
        {left, bottom, 0, 0, 1}, {right, top, 0, 1, 0}, {left, top, 0, 0, 0}
    };
    return upload_vertices(vertices, 6, shade, false);
}

static Mesh sky_mesh(void) {
    const float extent = 4096;
    const Position corners[8] = {{-extent, -extent, -extent}, {extent, -extent, -extent},
        {extent, extent, -extent}, {-extent, extent, -extent}, {-extent, -extent, extent},
        {extent, -extent, extent}, {extent, extent, extent}, {-extent, extent, extent}};
    const unsigned faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 4, 7, 3},
                                 {1, 5, 6, 2}, {0, 1, 5, 4}, {3, 2, 6, 7}};
    const unsigned order[6] = {0, 1, 2, 0, 2, 3};
    SceneVertex vertices[36];
    for (unsigned face = 0; face < 6; ++face) {
        for (unsigned index = 0; index < 6; ++index) {
            Position point = corners[faces[face][order[index]]];
            vertices[face * 6 + index] = (SceneVertex){point.x, point.y, point.z, 0, 0};
        }
    }
    return upload_vertices(vertices, 36, 0, true);
}

static Texture2D upload_texture(uint8_t *pixels, int width, int height, bool repeat) {
    Image image = {.data = pixels, .width = width, .height = height, .mipmaps = 1,
                   .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D texture = LoadTextureFromImage(image);
    if (!texture.id) fail("Texture upload failed");
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(texture, repeat ? TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
    return texture;
}

static Camera3D start_camera(const Scene *scene, bool overview) {
    Vector3 position = {scene->spawn.x, scene->spawn.y, scene->spawn.z};
    Camera3D camera = {.position = position, .up = {0, 1, 0}, .fovy = 70,
                       .projection = CAMERA_PERSPECTIVE};
    camera.target = Vector3Add(position, (Vector3){cosf(scene->yaw), 0, -sinf(scene->yaw)});
    if (overview) {
        Vector3 minimum = {1e6f, 1e6f, 1e6f}, maximum = {-1e6f, -1e6f, -1e6f};
        for (size_t batch = 0; batch < scene->batch_count; ++batch) {
            for (size_t index = 0; index < scene->batches[batch].count; ++index) {
                SceneVertex point = scene->batches[batch].vertices[index];
                minimum.x = fminf(minimum.x, point.x); minimum.z = fminf(minimum.z, point.z);
                maximum.x = fmaxf(maximum.x, point.x); maximum.z = fmaxf(maximum.z, point.z);
                maximum.y = fmaxf(maximum.y, point.y);
            }
        }
        camera.target = (Vector3){(minimum.x + maximum.x) * 0.5f, 0, (minimum.z + maximum.z) * 0.5f};
        float span = fmaxf(maximum.x - minimum.x, maximum.z - minimum.z);
        camera.position = Vector3Add(camera.target, (Vector3){0, maximum.y + span, span * 0.05f});
        camera.fovy = 60;
    }
    return camera;
}

static bool update_shade(Mesh *mesh, size_t first, size_t count, unsigned shade) {
    bool changed = false;
    for (size_t index = first; index < first + count; ++index) {
        if (mesh->colors[index * 4] == shade) continue;
        mesh->colors[index * 4] = (unsigned char)shade; changed = true;
    }
    return changed;
}

static unsigned sector_shade(const SceneSector *sector) { return (255 - sector->light) * 31 / 255; }

static void update_geometry(Scene *scene, const Specials *specials, Mesh *meshes, bool *dirty, const uint32_t *base_assets) {
    memset(dirty, 0, scene->batch_count * sizeof(bool));
    for (size_t index = 0; index < scene->batch_count; ++index) {
        Batch *batch = &scene->batches[index];
        if (batch->plane != SURFACE_FLOOR && batch->plane != SURFACE_CEILING) continue;
        dirty[index] = update_shade(&meshes[index], 0, batch->count, sector_shade(&scene->sectors[batch->owner]));
        float height = batch->plane == SURFACE_FLOOR ? scene->sectors[batch->owner].floor : scene->sectors[batch->owner].ceiling;
        if (meshes[index].vertices[1] == height) continue;
        for (size_t vertex = 0; vertex < batch->count; ++vertex) meshes[index].vertices[vertex * 3 + 1] = height;
        dirty[index] = true;
    }
    for (size_t index = 0; index < scene->wall_count; ++index) {
        const WallSurface *wall = &scene->walls[index];
        Batch *batch = &scene->batches[wall->batch];
        if (wall->switch_asset != UINT32_MAX)
            batch->asset = !scene->lines[wall->line].special || specials->switch_ticks[wall->line] ?
                           wall->switch_asset : base_assets[wall->batch];
        SceneVertex vertices[6]; wall_vertices(scene, wall, vertices);
        Mesh *mesh = &meshes[wall->batch];
        const CollisionLine *line = &scene->lines[wall->line];
        uint32_t sector = wall->side ? line->back : line->front;
        dirty[wall->batch] = update_shade(mesh, wall->first, 6, sector_shade(&scene->sectors[sector])) || dirty[wall->batch];
        for (size_t corner = 0; corner < 6; ++corner) {
            size_t vertex = wall->first + corner;
            float *position = mesh->vertices + vertex * 3, *uv = mesh->texcoords + vertex * 2;
            SceneVertex point = vertices[corner];
            if (position[0] == point.x && position[1] == point.y && position[2] == point.z &&
                uv[0] == point.u && uv[1] == point.v) continue;
            position[0] = point.x; position[1] = point.y; position[2] = point.z;
            uv[0] = point.u; uv[1] = point.v; dirty[wall->batch] = true;
        }
    }
    for (size_t index = 0; index < scene->batch_count; ++index) {
        if (!dirty[index]) continue;
        UpdateMeshBuffer(meshes[index], 0, meshes[index].vertices, (int)(scene->batches[index].count * 3 * sizeof(float)), 0);
        UpdateMeshBuffer(meshes[index], 1, meshes[index].texcoords, (int)(scene->batches[index].count * 2 * sizeof(float)), 0);
        UpdateMeshBuffer(meshes[index], 3, meshes[index].colors, (int)(scene->batches[index].count * 4), 0);
    }
}

static void move_camera(Scene *scene, Specials *specials, Player *player, Camera3D *camera,
                        float *yaw, float *pitch, bool *captured, bool noclip,
                        double *accumulator, bool *use_pending) {
    if (IsKeyPressed(KEY_TAB)) {
        *captured = !*captured;
        if (*captured) DisableCursor(); else EnableCursor();
    }
    if (*captured) {
        Vector2 mouse = GetMouseDelta();
        *yaw -= mouse.x * 0.0025f;
        if (noclip) *pitch = Clamp(*pitch - mouse.y * 0.0025f, -1.5f, 1.5f);
    }
    float seconds = fminf(GetFrameTime(), 0.25f);
    int forward_input = (int)IsKeyDown(KEY_W) - (int)IsKeyDown(KEY_S);
    int side_input = (int)IsKeyDown(KEY_D) - (int)IsKeyDown(KEY_A);
    bool running = IsKeyDown(KEY_LEFT_SHIFT);
    if (noclip) {
        float step = seconds * (running ? 20 : 7);
        Vector3 forward = {cosf(*yaw), 0, -sinf(*yaw)}, right = {sinf(*yaw), 0, cosf(*yaw)};
        Vector3 motion = Vector3Add(Vector3Scale(forward, (float)forward_input), Vector3Scale(right, (float)side_input));
        if (IsKeyDown(KEY_SPACE)) motion.y += 1;
        if (IsKeyDown(KEY_LEFT_CONTROL)) motion.y -= 1;
        if (Vector3LengthSqr(motion) > 0) camera->position = Vector3Add(camera->position, Vector3Scale(Vector3Normalize(motion), step));
    }
    if (!noclip && (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_SPACE))) *use_pending = true;
    if (!noclip && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) specials_shoot(scene, specials, player, *yaw);
    *accumulator += seconds;
    while (*accumulator >= 1.0 / 35) {
        if (specials->exited) { *accumulator = 0; break; }
        if (*use_pending) { specials_use(scene, specials, player, *yaw); *use_pending = false; }
        specials_tick(scene, specials, noclip ? NULL : player);
        if (!noclip) {
            Position previous = player->feet;
            bool can_move = player->health && !player->teleport_wait;
            player_tick(scene, player, can_move ? forward_input : 0, can_move ? side_input : 0, *yaw, running);
            player_pickup_keys(scene, player);
            if (specials_cross(scene, specials, previous, player->feet, player)) *yaw = specials->teleport_yaw;
            specials_player(scene, specials, player);
        }
        *accumulator -= 1.0 / 35;
    }
    if (!noclip) camera->position = (Vector3){player->feet.x, player_view_y(scene, player), player->feet.z};
    Vector3 direction = {cosf(*yaw) * cosf(*pitch), sinf(*pitch), -sinf(*yaw) * cosf(*pitch)};
    camera->target = Vector3Add(camera->position, direction);
}

int main(int argc, char **argv) {
    const char *path = "MAP01.drmap", *screenshot = NULL, *music_path = NULL;
    bool inspect = false, overview = false, noclip = false;
    bool no_music = false;
    bool all_keys = false;
    bool custom_view = false;
    float view[5] = {0};
    int frames = 0;
    for (int index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--inspect")) inspect = true;
        else if (!strcmp(argv[index], "--overview")) overview = true;
        else if (!strcmp(argv[index], "--noclip")) noclip = true;
        else if (!strcmp(argv[index], "--no-music")) no_music = true;
        else if (!strcmp(argv[index], "--all-keys")) all_keys = true;
        else if (!strcmp(argv[index], "--music") && index + 1 < argc) music_path = argv[++index];
        else if (!strcmp(argv[index], "--view") && index + 5 < argc) {
            for (unsigned component = 0; component < 5; ++component) {
                char *end; view[component] = strtof(argv[++index], &end);
                if (*end || !isfinite(view[component]) || fabsf(view[component]) > 1e7f) fail("Invalid view value");
            }
            custom_view = true; noclip = true;
        }
        else if (!strcmp(argv[index], "--screenshot") && index + 1 < argc) screenshot = argv[++index];
        else if (!strcmp(argv[index], "--frames") && index + 1 < argc) {
            char *end;
            long value = strtol(argv[++index], &end, 10);
            if (*end || value < 1 || value > 100000) fail("Invalid frame count");
            frames = (int)value;
        } else if (!strcmp(argv[index], "--help")) {
            puts("Usage: doom-raylib [MAP01.drmap] [--inspect] [--overview] [--noclip] [--frames N] [--screenshot FILE.png]\n"
                 "       [--music FILE.mid] [--no-music] [--all-keys] [--view X Y Z YAW PITCH]\n"
                 "WASD: walk | mouse: turn | E/Space: use | Shift: run | N: noclip | Tab: mouse | R: reset | Esc: quit\n"
                 "Space/Ctrl: up/down in noclip; return to walk resets the player start");
            return 0;
        } else if (argv[index][0] == '-') fail("Unknown option %s", argv[index]);
        else path = argv[index];
    }
    if (screenshot && !frames) frames = 2;
    char *current_path = NULL;
load_scene:
    Scene scene = scene_read(path);
    scene_print(&scene);
    if (inspect) { scene_free(&scene); return 0; }
    Player player = player_spawn(&scene);
    if (all_keys) player.keys = 7;
    Specials specials = specials_create(&scene);
    noclip = noclip || overview;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (frames ? FLAG_WINDOW_HIDDEN : FLAG_VSYNC_HINT));
    InitWindow(1280, 800, TextFormat("Doom %s - C23 / raylib", scene.map));
    if (!IsWindowReady()) fail("Cannot create OpenGL window");
    Shader shader = LoadShaderFromMemory(vertex_shader, fragment_shader);
    if (!shader.id || shader.id == rlGetShaderIdDefault()) fail("Cannot load palette shader");
    shader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(shader, "texture1");
    int camera_location = GetShaderLocation(shader, "cameraPosition");
    if (camera_location < 0 || shader.locs[SHADER_LOC_MAP_METALNESS] < 0) fail("Missing shader uniforms");
    Texture2D lut = upload_texture(scene.lut, 256, 32, false);
    Texture2D *textures = allocate(scene.asset_count, sizeof(Texture2D));
    for (size_t index = 0; index < scene.asset_count; ++index) {
        Asset *asset = &scene.assets[index];
        textures[index] = upload_texture(asset->pixels, (int)asset->width, (int)asset->height, true);
    }
    Mesh *meshes = allocate(scene.batch_count, sizeof(Mesh));
    bool *dirty = allocate(scene.batch_count, sizeof(bool));
    uint32_t *base_assets = allocate(scene.batch_count, sizeof(uint32_t));
    for (size_t index = 0; index < scene.batch_count; ++index) {
        Batch *batch = &scene.batches[index];
        base_assets[index] = batch->asset;
        meshes[index] = upload_vertices(batch->vertices, batch->count, batch->shade, batch->sky != 0);
    }
    Mesh *sprites = allocate(scene.sprite_count, sizeof(Mesh));
    for (size_t index = 0; index < scene.sprite_count; ++index)
        sprites[index] = sprite_mesh(&scene.assets[scene.sprites[index].asset], scene.sprites[index].shade);
    Material material = LoadMaterialDefault();
    material.shader = shader;
    material.maps[MATERIAL_MAP_METALNESS].texture = lut;
    uint32_t sky_asset = UINT32_MAX;
    for (size_t index = 0; index < scene.batch_count; ++index)
        if (scene.batches[index].sky) { sky_asset = scene.batches[index].asset; break; }
    Mesh sky = {0};
    if (sky_asset != UINT32_MAX) sky = sky_mesh();
    Camera3D camera = start_camera(&scene, overview);
    Vector3 initial_direction = Vector3Subtract(camera.target, camera.position);
    float yaw = atan2f(-initial_direction.z, initial_direction.x);
    float pitch = atan2f(initial_direction.y, hypotf(initial_direction.x, initial_direction.z));
    if (custom_view) {
        camera.position = (Vector3){view[0], view[1], view[2]};
        yaw = view[3] * DEG2RAD; pitch = view[4] * DEG2RAD;
        camera.target = Vector3Add(camera.position, (Vector3){cosf(yaw) * cosf(pitch), sinf(pitch), -sinf(yaw) * cosf(pitch)});
    }
    double accumulator = 0;
    bool use_pending = false;
    bool captured = !frames;
    if (captured) DisableCursor();
    char *default_music = scene_sidecar(path, ".mid");
    if (!no_music && (!frames || music_path)) {
        const char *song = music_path ? music_path : default_music;
        if (music_start(song)) printf("MIDI playback started: %s\n", song);
        else TraceLog(LOG_WARNING, "MIDI playback unavailable: %s", song);
    }
    rlSetClipPlanes(0.02, 10000);
    int frame = 0;
    char *next_map = NULL;
    bool exit_checked = false, next_exists = false, advance = false;
    while (!WindowShouldClose() && (!frames || frame < frames)) {
        music_update();
        if (!frames) {
            bool reset = IsKeyPressed(KEY_R);
            if (IsKeyPressed(KEY_N)) {
                noclip = !noclip;
                reset = reset || !noclip;
            }
            if (reset) {
                uint32_t keys = player.keys; player = player_spawn(&scene); player.keys = keys;
                camera = start_camera(&scene, false); yaw = scene.yaw; pitch = 0;
            }
            move_camera(&scene, &specials, &player, &camera, &yaw, &pitch, &captured, noclip, &accumulator, &use_pending);
        }
        if (!overview) camera.fovy = 2 * atanf((float)GetScreenHeight() / (float)GetScreenWidth()) * RAD2DEG;
        update_geometry(&scene, &specials, meshes, dirty, base_assets);
        for (size_t index = 0; index < scene.sprite_count; ++index) {
            const Sprite *sprite = &scene.sprites[index];
            unsigned shade = sprite->fullbright ? 0 : sector_shade(&scene.sectors[sprite->sector]);
            if (update_shade(&sprites[index], 0, 6, shade)) UpdateMeshBuffer(sprites[index], 3, sprites[index].colors, 24, 0);
        }
        if (specials.exited && !exit_checked) {
            next_map = specials_next_map(&scene, &specials, path); next_exists = next_map && FileExists(next_map);
            exit_checked = true;
        }
        SetShaderValue(shader, camera_location, &camera.position, SHADER_UNIFORM_VEC3);
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
        rlDisableBackfaceCulling();
        if (sky_asset != UINT32_MAX) {
            rlDisableDepthMask();
            material.maps[MATERIAL_MAP_ALBEDO].texture = textures[sky_asset];
            DrawMesh(sky, material, MatrixTranslate(camera.position.x, camera.position.y, camera.position.z));
            rlEnableDepthMask();
        }
        for (size_t index = 0; index < scene.batch_count; ++index) {
            Batch *batch = &scene.batches[index];
            if ((batch->plane == SURFACE_FLOOR || batch->plane == SURFACE_CEILING) &&
                scene.sectors[batch->owner].ceiling <= scene.sectors[batch->owner].floor) continue;
            material.maps[MATERIAL_MAP_ALBEDO].texture = textures[scene.batches[index].asset];
            DrawMesh(meshes[index], material, MatrixIdentity());
        }
        float sprite_yaw = atan2f(-(camera.target.z - camera.position.z), camera.target.x - camera.position.x) - PI * 0.5f;
        for (size_t index = 0; index < scene.sprite_count; ++index) {
            Sprite *sprite = &scene.sprites[index];
            if (sprite->hidden) continue;
            material.maps[MATERIAL_MAP_ALBEDO].texture = textures[sprite->asset];
            Matrix transform = MatrixMultiply(MatrixRotateY(sprite_yaw),
                MatrixTranslate(sprite->position.x, sprite->position.y, sprite->position.z));
            DrawMesh(sprites[index], material, transform);
        }
        rlEnableBackfaceCulling();
        EndMode3D();
        if (!frames) {
            DrawRectangle(0, 0, GetScreenWidth(), 32, (Color){0, 0, 0, 180});
            DrawText(noclip ? "NOCLIP | WASD + mouse | Space/Ctrl | N: walk | Shift | Tab | R | Esc" :
                             "WALK | WASD + mouse | E/Space: use | N: noclip | Shift | Tab | R | Esc", 12, 8, 16, RAYWHITE);
            DrawFPS(GetScreenWidth() - 90, 8);
            DrawRectangle(GetScreenWidth() - 180, 10, 12, 12, player.keys & 1 ? BLUE : DARKGRAY);
            DrawRectangle(GetScreenWidth() - 160, 10, 12, 12, player.keys & 2 ? YELLOW : DARKGRAY);
            DrawRectangle(GetScreenWidth() - 140, 10, 12, 12, player.keys & 4 ? RED : DARKGRAY);
            DrawText(TextFormat("Health %u | Secrets %u/%u", player.health, specials.secrets, specials.total_secrets),
                     12, 40, 18, RAYWHITE);
            if (specials.secret_message) DrawText("A secret is found!", 12, 66, 20, YELLOW);
            if (!player.health) DrawText("R: return to start", 12, 92, 20, RED);
            if (specials.exited) {
                DrawRectangle(0, 120, GetScreenWidth(), 120, (Color){0, 0, 0, 210});
                DrawText(TextFormat("%s complete | Secrets %u/%u", scene.map, specials.secrets, specials.total_secrets), 24, 144, 24, RAYWHITE);
                DrawText(next_exists ? "Enter: next map | Esc: quit" : next_map ?
                         "Next map is not prepared. Use convert-all.ps1. Esc: quit" : "End of Doom II | Esc: quit", 24, 188, 20, RAYWHITE);
                if (next_exists && IsKeyPressed(KEY_ENTER)) advance = true;
            }
        }
        if (screenshot && frame == frames - 1) {
            Image capture = LoadImageFromScreen();
            if (!ExportImage(capture, screenshot)) fail("Cannot write screenshot %s", screenshot);
            UnloadImage(capture);
        }
        EndDrawing();
        ++frame;
        if (advance) break;
    }
    printf("Rendered %d frames\n", frame);
    music_stop();
    free(default_music);
    if (sky_asset != UINT32_MAX) UnloadMesh(sky);
    for (size_t index = 0; index < scene.sprite_count; ++index) UnloadMesh(sprites[index]);
    for (size_t index = 0; index < scene.batch_count; ++index) UnloadMesh(meshes[index]);
    for (size_t index = 0; index < scene.asset_count; ++index) UnloadTexture(textures[index]);
    MemFree(material.maps);
    UnloadTexture(lut); UnloadShader(shader);
    free(sprites); free(meshes); free(textures);
    free(dirty); specials_free(&specials);
    free(base_assets);
    CloseWindow();
    scene_free(&scene);
    if (advance) {
        free(current_path); current_path = next_map; path = current_path; music_path = NULL;
        goto load_scene;
    }
    free(next_map); free(current_path);
    return 0;
}
