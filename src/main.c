#include "scene.h"
#include "movement.h"
#include "music.h"
#include "specials.h"
#include "renderer.h"
#include "level_prepare.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && defined(DOOM_DLSS5)
#include <windows.h>
#include <wchar.h>

static bool neural_relaunch(int *result) {
    wchar_t executable[32768];
    DWORD length = GetModuleFileNameW(NULL, executable, 32768);
    if (!length || length >= 32768) fail("Cannot resolve the neural-rendering executable path");
    wchar_t *name = wcsrchr(executable, L'\\');
    if (!name) fail("Neural-rendering executable needs an absolute path");
    ++name;
    if (!_wcsicmp(name, L"nvngx.dll")) return false;
    if ((size_t)(name - executable) + 10 > 32768) fail("Neural-rendering executable path is too long");
    memcpy(name, L"nvngx.dll", sizeof(L"nvngx.dll"));
    const wchar_t *original = GetCommandLineW();
    size_t command_size = (wcslen(original) + 1) * sizeof(wchar_t);
    wchar_t *command = allocate(command_size, 1);
    memcpy(command, original, command_size);
    STARTUPINFOW startup = {.cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES,
                            .hStdInput = GetStdHandle(STD_INPUT_HANDLE),
                            .hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE),
                            .hStdError = GetStdHandle(STD_ERROR_HANDLE)};
    PROCESS_INFORMATION process = {0};
    BOOL started = CreateProcessW(executable, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                  NULL, NULL, &startup, &process);
    DWORD launch_error = started ? 0 : GetLastError();
    free(command);
    if (!started) fail("Cannot launch nvngx.dll (Windows error %lu); rebuild the DLSS target", launch_error);
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0)
        fail("Cannot wait for the neural-rendering process");
    DWORD status = 1;
    if (!GetExitCodeProcess(process.hProcess, &status)) fail("Cannot read the neural-rendering process result");
    CloseHandle(process.hProcess);
    *result = (int)status;
    return true;
}
#endif

static const float radians = 0.017453292519943295f;

typedef struct {
    bool pressed[GLFW_KEY_LAST + 1], shoot, have_cursor;
    double cursor_x, cursor_y, mouse_x, mouse_y;
} Input;

static void key_event(GLFWwindow *window, int key, int scan, int action, int modifiers) {
    (void)scan; (void)modifiers;
    Input *input = glfwGetWindowUserPointer(window);
    if (key >= 0 && key <= GLFW_KEY_LAST && action == GLFW_PRESS) input->pressed[key] = true;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
}

static void mouse_button_event(GLFWwindow *window, int button, int action, int modifiers) {
    (void)modifiers;
    Input *input = glfwGetWindowUserPointer(window);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) input->shoot = true;
}

static void cursor_event(GLFWwindow *window, double horizontal, double vertical) {
    Input *input = glfwGetWindowUserPointer(window);
    if (input->have_cursor) {
        input->mouse_x += horizontal - input->cursor_x;
        input->mouse_y += vertical - input->cursor_y;
    }
    input->cursor_x = horizontal; input->cursor_y = vertical; input->have_cursor = true;
}

static void focus_event(GLFWwindow *window, int focused) {
    (void)focused;
    Input *input = glfwGetWindowUserPointer(window);
    input->have_cursor = false;
    input->mouse_x = input->mouse_y = 0;
}

static void window_error(int code, const char *message) {
    fprintf(stderr, "GLFW error %d: %s\n", code, message);
}

static Position position_add(Position first, Position second) {
    return (Position){first.x + second.x, first.y + second.y, first.z + second.z};
}

static Position position_scale(Position value, float scale) {
    return (Position){value.x * scale, value.y * scale, value.z * scale};
}

static float position_length_squared(Position value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

static RenderCamera start_camera(const Scene *scene, bool overview) {
    Position position = {scene->spawn.x, scene->spawn.y, scene->spawn.z};
    RenderCamera camera = {.position = position, .fovy = 70};
    camera.target = position_add(position, (Position){cosf(scene->yaw), 0, -sinf(scene->yaw)});
    if (overview) {
        Position minimum = {1e6f, 1e6f, 1e6f}, maximum = {-1e6f, -1e6f, -1e6f};
        for (size_t batch = 0; batch < scene->batch_count; ++batch) {
            for (size_t index = 0; index < scene->batches[batch].count; ++index) {
                SceneVertex point = scene->batches[batch].vertices[index];
                minimum.x = fminf(minimum.x, point.x); minimum.z = fminf(minimum.z, point.z);
                maximum.x = fmaxf(maximum.x, point.x); maximum.z = fmaxf(maximum.z, point.z);
                maximum.y = fmaxf(maximum.y, point.y);
            }
        }
        camera.target = (Position){(minimum.x + maximum.x) * 0.5f, 0, (minimum.z + maximum.z) * 0.5f};
        float span = fmaxf(maximum.x - minimum.x, maximum.z - minimum.z);
        camera.position = position_add(camera.target, (Position){0, maximum.y + span, span * 0.05f});
        camera.fovy = 60;
    }
    return camera;
}

static bool move_camera(GLFWwindow *window, Input *input, float seconds, Scene *scene, Specials *specials, Player *player, RenderCamera *camera,
                        float *yaw, float *pitch, bool *captured, bool noclip,
                        double *accumulator, bool *use_pending) {
    bool reset_history = false;
    if (input->pressed[GLFW_KEY_TAB]) {
        *captured = !*captured;
        glfwSetInputMode(window, GLFW_CURSOR, *captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        input->have_cursor = false;
        input->mouse_x = input->mouse_y = 0;
    }
    if (*captured) {
        float mouse_x = (float)input->mouse_x, mouse_y = (float)input->mouse_y;
        *yaw -= mouse_x * 0.0025f;
        if (noclip) *pitch = fmaxf(-1.5f, fminf(1.5f, *pitch - mouse_y * 0.0025f));
    }
    int forward_input = (int)(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) - (int)(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
    int side_input = (int)(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) - (int)(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
    bool running = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
    if (noclip) {
        float step = seconds * (running ? 20 : 7);
        Position forward = {cosf(*yaw), 0, -sinf(*yaw)}, right = {sinf(*yaw), 0, cosf(*yaw)};
        Position motion = position_add(position_scale(forward, (float)forward_input), position_scale(right, (float)side_input));
        if ((glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)) motion.y += 1;
        if ((glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)) motion.y -= 1;
        if (position_length_squared(motion) > 0) camera->position = position_add(camera->position, position_scale(position_scale(motion, 1 / sqrtf(position_length_squared(motion))), step));
    }
    if (!noclip && (input->pressed[GLFW_KEY_E] || input->pressed[GLFW_KEY_SPACE])) *use_pending = true;
    if (!noclip && input->shoot) specials_shoot(scene, specials, player, *yaw);
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
            if (specials_cross(scene, specials, previous, player->feet, player)) {
                *yaw = specials->teleport_yaw; reset_history = true;
            }
            specials_player(scene, specials, player);
        }
        *accumulator -= 1.0 / 35;
    }
    if (!noclip) camera->position = (Position){player->feet.x, player_view_y(scene, player), player->feet.z};
    Position direction = {cosf(*yaw) * cosf(*pitch), sinf(*pitch), -sinf(*yaw) * cosf(*pitch)};
    camera->target = position_add(camera->position, direction);
    return reset_history;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *path = "MAP01.drmap", *screenshot = NULL, *music_path = NULL, *motion_capture = NULL;
    bool inspect = false, overview = false, noclip = false;
    bool no_music = false, validation = false, dlss_enabled = false;
    bool profile = false, frame_generation = false, visible = false, ray_lighting = false;
    bool no_vsync = false;
    bool no_neural = false;
    bool no_ngx = false;
    bool fullscreen = false;
    bool voxels = false;
    int voxel_mode = VOXELS_AUTO;
    bool have_path = false;
    const char *requested_map = NULL;
    bool prepare_all = false;
#ifdef DOOM_DLSS5
    dlss_enabled = true;
#endif
#ifdef DOOM_FRAME_GENERATION
    bool fg_available = true;
#else
    bool fg_available = false;
#endif
    bool dlss_available = dlss_enabled;
    bool ray_reconstruction = false;
    ray_lighting = true;
    unsigned neural_style = 1, neural_passes = 2;
    unsigned upscale_mode = 3;
    int window_size[2] = {1280, 800};
    bool all_keys = false;
    bool custom_view = false;
    float view[5] = {0};
    int frames = 0;
    for (int index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--inspect")) inspect = true;
        else if (!strcmp(argv[index], "--prepare-all")) prepare_all = true;
        else if (!strcmp(argv[index], "--voxels")) voxel_mode = VOXELS_ON;
        else if (!strcmp(argv[index], "--no-voxels")) voxel_mode = VOXELS_OFF;
        else if (!strcmp(argv[index], "--validation")) validation = true;
        else if (!strcmp(argv[index], "--dlss")) dlss_enabled = true;
        else if (!strcmp(argv[index], "--dlss5")) { dlss_enabled = true; ray_reconstruction = false; }
        else if (!strcmp(argv[index], "--no-dlss")) dlss_enabled = false;
        else if (!strcmp(argv[index], "--no-nr")) no_neural = true;
        else if (!strcmp(argv[index], "--no-ngx")) no_ngx = true;
        else if (!strcmp(argv[index], "--profile")) profile = true;
        else if (!strcmp(argv[index], "--fg")) frame_generation = true;
        else if (!strcmp(argv[index], "--no-fg")) frame_generation = false;
        else if (!strcmp(argv[index], "--ray-lighting")) ray_lighting = true;
        else if (!strcmp(argv[index], "--no-ray-lighting")) ray_lighting = false;
        else if (!strcmp(argv[index], "--rr")) ray_reconstruction = true;
        else if (!strcmp(argv[index], "--no-rr")) ray_reconstruction = false;
        else if (!strcmp(argv[index], "--no-vsync")) no_vsync = true;
        else if (!strcmp(argv[index], "--fullscreen")) fullscreen = true;
        else if (!strcmp(argv[index], "--visible")) visible = true;
        else if (!strcmp(argv[index], "--sr") && index + 1 < argc) {
            const char *mode = argv[++index];
            if (!strcmp(mode, "off")) upscale_mode = 0;
            else if (!strcmp(mode, "quality")) upscale_mode = 1;
            else if (!strcmp(mode, "balanced")) upscale_mode = 2;
            else if (!strcmp(mode, "performance")) upscale_mode = 3;
            else fail("SR mode must be off, quality, balanced, or performance");
        }
        else if (!strcmp(argv[index], "--size") && index + 2 < argc) {
            for (unsigned dimension = 0; dimension < 2; ++dimension) {
                char *end;
                long value = strtol(argv[++index], &end, 10);
                if (*end || value < 64 || value > 8192) fail("Window size must be 64 through 8192 pixels");
                window_size[dimension] = (int)value;
            }
        }
        else if (!strcmp(argv[index], "--nr-style") && index + 1 < argc) {
            char *end;
            long value = strtol(argv[++index], &end, 10);
            if (*end || value < 0 || value > 2) fail("Neural style must be 0, 1, or 2");
            neural_style = (unsigned)value;
        }
        else if (!strcmp(argv[index], "--nr-passes") && index + 1 < argc) {
            char *end;
            long value = strtol(argv[++index], &end, 10);
            if (*end || value < 1 || value > 4) fail("Neural pass count must be 1 through 4");
            neural_passes = (unsigned)value;
        }
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
        else if (!strcmp(argv[index], "--motion") && index + 1 < argc) motion_capture = argv[++index];
        else if (!strcmp(argv[index], "--frames") && index + 1 < argc) {
            char *end;
            long value = strtol(argv[++index], &end, 10);
            if (*end || value < 1 || value > 100000) fail("Invalid frame count");
            frames = (int)value;
        } else if (!strcmp(argv[index], "--help")) {
            puts("Usage: doom-raylib [MAP01.drmap] [--inspect] [--overview] [--noclip] [--frames N] [--screenshot FILE.png]\n"
                 "       doom-raylib INPUT.WAD [MAP01..MAP32] [options] (prepare maps and music automatically)\n"
                 "       doom-raylib INPUT.WAD --prepare-all [--voxels|--no-voxels] (fill cache, no GPU)\n"
                 "       [--music FILE.mid] [--no-music] [--all-keys] [--validation] [--view X Y Z YAW PITCH]\n"
                 "       [--motion FILE.pfm] [--dlss|--no-dlss] [--nr-style 0|1|2] [--nr-passes 1..4] [--profile]\n"
                 "       [--sr off|quality|balanced|performance] [--size WIDTH HEIGHT] [--fg] [--visible] [--fullscreen]\n"
                 "       [--no-fg] [--ray-lighting|--no-ray-lighting] [--no-vsync]\n"
                 "       [--rr|--no-rr (F10: DLSS Ray Reconstruction)] [--no-nr]\n"
                 "       [--no-ngx (native rendering; do not initialize any NGX feature)]\n"
                 "       [--voxels|--no-voxels (WAD: auto when source assets exist; drmap: off by default)]\n"
                 "Defaults: DLSS5 NR+SR when built in (style 1, two passes), FG off, ray lighting on.\n"
                 "--dlss5 selects NR+SR; --rr selects Ray Reconstruction. F10 switches between them.\n"
                 "WASD: walk | mouse: turn | E/Space: use | Shift: run | N: noclip | Tab: mouse | R: reset | Esc: quit\n"
                 "Space/Ctrl: up/down in noclip; return to walk resets the player start\n"
                 "F5: DLSS | F6: NR | F7: style | F8: 1/2/4 NR passes | F9: FG | F10: RR | F12: fullscreen | L: ray lighting\n"
                 "The scene renders at 640x480; the window is the output resolution.");
            return 0;
        } else if (argv[index][0] == '-') fail("Unknown option %s", argv[index]);
        else if (!have_path) { path = argv[index]; have_path = true; }
        else if (!requested_map) requested_map = argv[index];
        else fail("Unexpected argument %s", argv[index]);
    }
    if ((screenshot || motion_capture) && !frames) frames = 2;
    if (no_ngx) {
        dlss_available = fg_available = dlss_enabled = frame_generation = ray_reconstruction = false;
        puts("NGX disabled for this process; native rendering and ray lighting remain available");
    }
    if (dlss_enabled && !dlss_available) fail("DLSS requires a DOOM_DLSS5 build");
    if (frame_generation && !fg_available) fail("FG requires a DOOM_FRAME_GENERATION build");
    if (ray_reconstruction && !dlss_available) fail("Ray reconstruction requires a DOOM_DLSS5 build");
#if defined(_WIN32) && defined(DOOM_DLSS5)
    if (dlss_available && !inspect && !prepare_all) {
        int status;
        if (neural_relaunch(&status)) return status;
    }
#endif
    char *current_path = NULL;
    LevelSource source = {0};
    if (level_is_wad(path)) {
        source = level_source_create(path, voxel_mode);
        if (prepare_all) {
            for (unsigned number = 1; number <= 32; ++number) {
                char map[6];
                snprintf(map, sizeof(map), "MAP%02u", number);
                char *prepared = level_source_prepare(&source, map);
                free(prepared);
            }
            puts("Prepared MAP01..MAP32 in the cache");
            level_source_free(&source);
            return 0;
        }
        current_path = level_source_prepare(&source, requested_map ? requested_map : "MAP01");
        path = current_path;
        voxels = source.voxel_root != NULL;
    } else {
        if (prepare_all) fail("--prepare-all requires WAD input");
        if (requested_map) fail("A map name is only valid with WAD input");
        voxels = voxel_mode == VOXELS_ON;
    }
    bool neural_active = dlss_enabled && !no_neural;
    bool ray_active = ray_lighting;
    glfwSetErrorCallback(window_error);
    if (!inspect && !glfwInit()) fail("Cannot initialize GLFW");
load_scene:
    Scene scene = scene_read(path);
    if (voxels) {
        char *voxel_path = scene_sidecar(path, ".drvox");
        scene_read_voxels(&scene, voxel_path);
        free(voxel_path);
    }
    scene_print(&scene);
    if (inspect) { scene_free(&scene); free(current_path); level_source_free(&source); return 0; }
    Player player = player_spawn(&scene);
    if (all_keys) player.keys = 7;
    Specials specials = specials_create(&scene);
    noclip = noclip || overview;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, frames && !visible ? GLFW_FALSE : GLFW_TRUE);
    char title[128];
    snprintf(title, sizeof(title), "Doom %s - C23 / Vulkan", scene.map);
    GLFWmonitor *monitor = fullscreen ? glfwGetPrimaryMonitor() : NULL;
    const GLFWvidmode *video_mode = monitor ? glfwGetVideoMode(monitor) : NULL;
    GLFWwindow *window = glfwCreateWindow(video_mode ? video_mode->width : window_size[0],
        video_mode ? video_mode->height : window_size[1], title, monitor, NULL);
    if (!window) fail("Cannot create Vulkan window");
    int window_position[2] = {100, 100};
    Input input = {0};
    glfwSetWindowUserPointer(window, &input);
    glfwSetKeyCallback(window, key_event);
    glfwSetMouseButtonCallback(window, mouse_button_event);
    glfwSetCursorPosCallback(window, cursor_event);
    glfwSetWindowFocusCallback(window, focus_event);
    Renderer *renderer = renderer_create(window, &scene, (RendererOptions){.validation = validation,
        .dlss_enabled = dlss_available, .upscale_mode = dlss_enabled ? upscale_mode : 0,
        .frame_generation = fg_available, .ray_lighting = true, .ray_reconstruction = dlss_available, .no_vsync = no_vsync});
    if (profile) renderer_enable_profile(renderer);
    if (dlss_available) renderer_set_neural_style(renderer, neural_style);
    if (dlss_available) renderer_set_neural_passes(renderer, neural_passes);
    if (dlss_available && !neural_active) renderer_set_neural(renderer, false);
    if (fg_available && !frame_generation) renderer_set_frame_generation(renderer, false);
    if (!ray_active) renderer_set_ray_lighting(renderer, false);
    renderer_set_ray_reconstruction(renderer, ray_reconstruction && dlss_enabled && ray_active);
    RenderCamera camera = start_camera(&scene, overview);
    Position initial_direction = position_add(camera.target, position_scale(camera.position, -1));
    float yaw = atan2f(-initial_direction.z, initial_direction.x);
    float pitch = atan2f(initial_direction.y, hypotf(initial_direction.x, initial_direction.z));
    if (custom_view) {
        camera.position = (Position){view[0], view[1], view[2]};
        yaw = view[3] * radians; pitch = view[4] * radians;
        camera.target = position_add(camera.position, (Position){cosf(yaw) * cosf(pitch), sinf(pitch), -sinf(yaw) * cosf(pitch)});
    }
    double accumulator = 0, last_time = glfwGetTime();
    bool use_pending = false, captured = !frames;
    if (captured) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    char *default_music = scene_sidecar(path, ".mid");
    if (!no_music && (!frames || music_path)) {
        const char *song = music_path ? music_path : default_music;
        if (music_start(song)) printf("MIDI playback started: %s\n", song);
        else fprintf(stderr, "MIDI playback unavailable: %s\n", song);
    }
    int frame = 0;
    char *next_map = NULL;
    bool exit_checked = false, next_exists = false, advance = false;
    while (!glfwWindowShouldClose(window) && (!frames || frame < frames)) {
        memset(input.pressed, 0, sizeof(input.pressed));
        input.shoot = false; input.mouse_x = input.mouse_y = 0;
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) glfwWaitEventsTimeout(0.02);
        double now = glfwGetTime();
        double frame_seconds = now - last_time;
        float seconds = (float)fmin(frame_seconds, 0.25);
        last_time = now;
        music_update();
        bool reset_history = frame == 0;
        if (!frames) {
            if (input.pressed[GLFW_KEY_F12]) {
                if (frame_generation) renderer_set_frame_generation(renderer, false);
                fullscreen = glfwGetWindowMonitor(window) == NULL;
                if (fullscreen) {
                    glfwGetWindowPos(window, &window_position[0], &window_position[1]);
                    glfwGetWindowSize(window, &window_size[0], &window_size[1]);
                    monitor = glfwGetPrimaryMonitor();
                    video_mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(window, monitor, 0, 0, video_mode->width, video_mode->height, video_mode->refreshRate);
                } else {
                    glfwSetWindowMonitor(window, NULL, window_position[0], window_position[1],
                        window_size[0], window_size[1], GLFW_DONT_CARE);
                }
                if (frame_generation) renderer_set_frame_generation(renderer, true);
                reset_history = true;
            }
            if (input.pressed[GLFW_KEY_L] && renderer_set_ray_lighting(renderer, !ray_active))
                ray_active = !ray_active;
            if (dlss_available && input.pressed[GLFW_KEY_F5]) {
                dlss_enabled = !dlss_enabled;
                neural_active = dlss_enabled;
                renderer_set_neural(renderer, neural_active);
                renderer_set_upscale_mode(renderer, dlss_enabled ? upscale_mode : 0);
                reset_history = true;
            }
            if (fg_available && input.pressed[GLFW_KEY_F9] &&
                renderer_set_frame_generation(renderer, !frame_generation)) frame_generation = !frame_generation;
            if (dlss_available && input.pressed[GLFW_KEY_F10]) ray_reconstruction = !ray_reconstruction;
            renderer_set_ray_reconstruction(renderer, ray_reconstruction && dlss_enabled && ray_active);
            if (dlss_enabled && input.pressed[GLFW_KEY_F6] && renderer_set_neural(renderer, !neural_active)) {
                neural_active = !neural_active;
                reset_history = true;
            }
            if (dlss_enabled && input.pressed[GLFW_KEY_F7]) {
                neural_style = (neural_style + 1) % 3;
                renderer_set_neural_style(renderer, neural_style);
                reset_history = true;
            }
            if (dlss_enabled && input.pressed[GLFW_KEY_F8]) {
                neural_passes = neural_passes == 1 ? 2 : neural_passes == 2 ? 4 : 1;
                renderer_set_neural_passes(renderer, neural_passes);
                reset_history = true;
            }
            bool reset = input.pressed[GLFW_KEY_R];
            if (input.pressed[GLFW_KEY_N]) {
                noclip = !noclip;
                reset = reset || !noclip;
                reset_history = true;
            }
            if (reset) {
                uint32_t keys = player.keys; player = player_spawn(&scene); player.keys = keys;
                camera = start_camera(&scene, false); yaw = scene.yaw; pitch = 0;
                reset_history = true;
            }
            reset_history = move_camera(window, &input, seconds, &scene, &specials, &player, &camera,
                                        &yaw, &pitch, &captured, noclip, &accumulator, &use_pending) || reset_history;
        }
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) { glfwWaitEventsTimeout(0.05); continue; }
        if (!overview) camera.fovy = 2 * atanf((float)height / (float)width) / radians;
        if (specials.exited && !exit_checked) {
            next_map = specials_next_map(&scene, &specials, path);
            next_exists = next_map && (source.wad || file_exists(next_map));
            exit_checked = true;
        }
        if (!frames) {
            renderer_text(renderer, noclip ? "NOCLIP | WASD + mouse | Space/Ctrl | N: walk | Shift | Tab | R | F12 | Esc" :
                          "WALK | WASD + mouse | E/Space: use | N: noclip | Shift | Tab | R | F12 | Esc", 12, 8);
            char status[384];
            char mode[192];
            bool rr_active = ray_reconstruction && dlss_enabled && ray_active;
            snprintf(mode, sizeof(mode), "%s | NR S%u x%u%s | F5:DLSS F6:NR F7:style F8:passes | FG %s [F9] | F10:mode",
                                       rr_active ? "DLSS RR" : neural_active ? "DLSS5 NR" : dlss_enabled && upscale_mode ? "SR only" : "Native", neural_style, neural_passes,
                                       rr_active ? " paused" : neural_active ? " on" : " off", frame_generation ? "2x" : "off");
            snprintf(status, sizeof(status), "Health %u | Secrets %u/%u | Keys %s %s %s | %.0f render FPS | %s%s",
                     player.health, specials.secrets, specials.total_secrets,
                     player.keys & 1 ? "BLUE" : "-", player.keys & 2 ? "YELLOW" : "-",
                     player.keys & 4 ? "RED" : "-", frame_seconds > 0 ? 1.0 / frame_seconds : 0,
                     mode, ray_active ? " | RT on [L]" : " | RT off [L]");
            renderer_text(renderer, status, 12, 32);
            if (specials.secret_message) renderer_text(renderer, "A secret is found!", 12, 56);
            if (!player.health) renderer_text(renderer, "R: return to start", 12, 80);
            if (specials.exited) {
                snprintf(status, sizeof(status), "%s complete | Secrets %u/%u", scene.map, specials.secrets, specials.total_secrets);
                renderer_text(renderer, status, 24, 144);
                renderer_text(renderer, next_exists ? "Enter: next map | Esc: quit" : next_map ?
                              "Next map is not prepared. Use convert-all.ps1. Esc: quit" : "End of Doom II | Esc: quit", 24, 188);
                if (next_exists && input.pressed[GLFW_KEY_ENTER]) advance = true;
            }
        }
        renderer_draw(renderer, &scene, &specials, &camera, reset_history);
        if (screenshot && frame == frames - 1) renderer_capture(renderer, screenshot);
        if (motion_capture && frame == frames - 1) renderer_capture_motion(renderer, motion_capture);
        ++frame;
        if (advance) break;
    }
    printf("Rendered %d frames\n", frame);
    music_stop();
    free(default_music);
    renderer_destroy(renderer);
    specials_free(&specials);
    glfwDestroyWindow(window);
    scene_free(&scene);
    if (advance) {
        if (source.wad) {
            char map[6];
            const char *name = strrchr(next_map, '/'), *backslash = strrchr(next_map, '\\');
            if (backslash && (!name || backslash > name)) name = backslash;
            memcpy(map, name ? name + 1 : next_map, 5); map[5] = 0;
            free(next_map);
            next_map = level_source_prepare(&source, map);
        }
        free(current_path); current_path = next_map; path = current_path; music_path = NULL;
        goto load_scene;
    }
    free(next_map); free(current_path);
    level_source_free(&source);
    glfwTerminate();
    return 0;
}
