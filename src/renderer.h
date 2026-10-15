#pragma once

#include "scene.h"
#include "specials.h"

typedef struct GLFWwindow GLFWwindow;
typedef struct Renderer Renderer;
typedef struct { Position position, target; float fovy; } RenderCamera;
typedef struct {
    bool validation, dlss_enabled, frame_generation, ray_lighting, no_vsync;
    unsigned upscale_mode;
} RendererOptions;

Renderer *renderer_create(GLFWwindow *window, const Scene *scene, RendererOptions options);
bool renderer_set_ray_lighting(Renderer *renderer, bool enabled);
void renderer_text(Renderer *renderer, const char *text, int horizontal, int vertical);
void renderer_draw(Renderer *renderer, Scene *scene, const Specials *specials,
                   const RenderCamera *camera, bool reset_history);
bool renderer_set_neural(Renderer *renderer, bool enabled);
bool renderer_set_upscale_mode(Renderer *renderer, unsigned mode);
bool renderer_set_frame_generation(Renderer *renderer, bool enabled);
bool renderer_set_neural_style(Renderer *renderer, unsigned style);
bool renderer_set_neural_passes(Renderer *renderer, unsigned passes);
void renderer_enable_profile(Renderer *renderer);
void renderer_capture(Renderer *renderer, const char *path);
const uint8_t *renderer_pixels(Renderer *renderer, uint32_t *width, uint32_t *height);
const uint8_t *renderer_generated_pixels(Renderer *renderer, uint32_t *width, uint32_t *height);
void renderer_capture_motion(Renderer *renderer, const char *path);
float renderer_motion_stats(Renderer *renderer);
void renderer_destroy(Renderer *renderer);
