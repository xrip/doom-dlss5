#pragma once

#include "scene.h"

enum { MAX_RAY_LIGHTS = 32 };

typedef struct {
    float position[4];
    float color[4];
    uint32_t meta[4];
} RayLight;

typedef struct SceneLights SceneLights;

SceneLights *scene_lights_create(const Scene *scene);
unsigned scene_lights_collect(SceneLights *lights, const Scene *scene, Position camera,
                              RayLight output[MAX_RAY_LIGHTS]);
void scene_lights_destroy(SceneLights *lights);
