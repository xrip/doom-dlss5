#include "lighting.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float x, z;
    double area;
} SectorGeometry;

typedef struct {
    float color[3];
    float power, radius, height;
    bool fullbright_only, valid;
} AssetLight;

typedef struct {
    RayLight light;
    float rank;
} RankedLight;

struct SceneLights {
    size_t sector_count, asset_count;
    SectorGeometry *sectors;
    AssetLight *assets;
    size_t *neighbor_offsets;
    uint32_t *neighbors;
};

typedef struct {
    const char *prefix;
    float power, radius;
    bool fullbright_only;
} LightRule;

static const LightRule light_rules[] = {
    {"TBLU", 2.0f, 5.5f, false}, {"TGRN", 2.0f, 5.5f, false},
    {"TRED", 2.0f, 5.5f, false}, {"CBRA", 1.2f, 4.0f, false},
    {"CAND", 0.8f, 3.2f, false}, {"FCAN", 0.8f, 3.2f, false},
    {"SMBT", 1.8f, 5.0f, false}, {"SMGT", 1.8f, 5.0f, false},
    {"SMRT", 1.8f, 5.0f, false}, {"TLMP", 2.4f, 6.0f, false},
    {"TLP2", 2.4f, 6.0f, false}, {"CEYE", 1.4f, 4.5f, false},
    {"FSKU", 1.6f, 4.5f, false}, {"SOUL", 1.8f, 4.8f, false},
    {"MEGA", 2.2f, 5.5f, false}, {"PINV", 1.5f, 4.2f, false},
    {"PINS", 1.3f, 4.0f, false}, {"PMAP", 0.9f, 3.5f, false},
    {"PVIS", 1.0f, 3.8f, true}, {"SKUL", 1.2f, 4.0f, false},
    {"ARM1", 0.55f, 2.8f, true}, {"ARM2", 0.55f, 2.8f, true},
    {"BKEY", 0.45f, 2.5f, true}, {"YKEY", 0.45f, 2.5f, true},
    {"RKEY", 0.45f, 2.5f, true}, {"RSKU", 0.45f, 2.5f, true},
    {"YSKU", 0.45f, 2.5f, true}, {"BSKU", 0.45f, 2.5f, true}
};

static const LightRule *asset_rule(const Asset *asset) {
    for (size_t index = 0; index < sizeof(light_rules) / sizeof(light_rules[0]); ++index)
        if (!memcmp(asset->name, light_rules[index].prefix, 4)) return &light_rules[index];
    return NULL;
}

static float linear_channel(uint8_t value) {
    float channel = (float)value / 255.0f;
    return channel <= 0.04045f ? channel / 12.92f :
        powf((channel + 0.055f) / 1.055f, 2.4f);
}

static bool asset_color(const Scene *scene, const Asset *asset, float color[3], float *height) {
    if (!asset->pixels || !asset->width || !asset->height ||
        (size_t)asset->height > SIZE_MAX / (size_t)asset->width) return false;
    size_t count = (size_t)asset->width * asset->height;
    double sums[3] = {0};
    double total_weight = 0;
    double height_sum = 0;
    for (size_t index = 0; index < count; ++index) {
        const uint8_t *pixel = asset->pixels + index * 4;
        if (!pixel[3]) continue;
        const uint8_t *palette = scene->lut + (size_t)pixel[0] * 4;
        float linear[3], peak = 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            linear[channel] = linear_channel(palette[channel]);
            peak = fmaxf(peak, linear[channel]);
        }
        double weight = (double)pixel[3] / 255.0 * peak * peak * peak * peak;
        for (unsigned channel = 0; channel < 3; ++channel)
            sums[channel] += (double)linear[channel] * weight;
        height_sum += ((double)asset->top - (double)(index / asset->width) - 0.5) / 32.0 * weight;
        total_weight += weight;
    }
    if (total_weight == 0) return false;
    *height = (float)(height_sum / total_weight);
    float maximum = 0;
    for (unsigned channel = 0; channel < 3; ++channel) {
        color[channel] = (float)(sums[channel] / total_weight);
        maximum = fmaxf(maximum, color[channel]);
    }
    if (maximum <= 0.0001f) return false;
    for (unsigned channel = 0; channel < 3; ++channel) color[channel] /= maximum;
    return true;
}

static void build_sector_geometry(SceneLights *lights, const Scene *scene) {
    double *largest = allocate(lights->sector_count, sizeof(*largest));
    for (size_t index = 0; index < scene->region_count; ++index) {
        const CollisionTriangle *triangle = &scene->regions[index];
        if (triangle->sector >= lights->sector_count) continue;
        const MapPoint *first = &triangle->points[0], *second = &triangle->points[1],
                       *third = &triangle->points[2];
        double cross = ((double)second->x - first->x) * ((double)third->z - first->z) -
                       ((double)second->z - first->z) * ((double)third->x - first->x);
        double area = fabs(cross) * 0.5;
        if (!isfinite(area) || area <= 0) continue;
        SectorGeometry *geometry = &lights->sectors[triangle->sector];
        geometry->area += area;
        if (area > largest[triangle->sector]) {
            largest[triangle->sector] = area;
            geometry->x = (first->x + second->x + third->x) / 3.0f;
            geometry->z = (first->z + second->z + third->z) / 3.0f;
        }
    }
    free(largest);
}

static bool valid_portal(const Scene *scene, const CollisionLine *line) {
    return line->front < scene->sector_count && line->back < scene->sector_count &&
           line->front != line->back;
}

static void build_neighbors(SceneLights *lights, const Scene *scene) {
    lights->neighbor_offsets = allocate(lights->sector_count + 1, sizeof(*lights->neighbor_offsets));
    size_t neighbor_count = 0;
    for (size_t index = 0; index < scene->line_count; ++index) {
        const CollisionLine *line = &scene->lines[index];
        if (!valid_portal(scene, line)) continue;
        ++lights->neighbor_offsets[line->front + 1];
        ++lights->neighbor_offsets[line->back + 1];
        neighbor_count += 2;
    }
    for (size_t index = 1; index <= lights->sector_count; ++index)
        lights->neighbor_offsets[index] += lights->neighbor_offsets[index - 1];
    lights->neighbors = allocate(neighbor_count, sizeof(*lights->neighbors));
    size_t *cursor = allocate(lights->sector_count, sizeof(*cursor));
    if (lights->sector_count)
        memcpy(cursor, lights->neighbor_offsets, lights->sector_count * sizeof(*cursor));
    for (size_t index = 0; index < scene->line_count; ++index) {
        const CollisionLine *line = &scene->lines[index];
        if (!valid_portal(scene, line)) continue;
        lights->neighbors[cursor[line->front]++] = line->back;
        lights->neighbors[cursor[line->back]++] = line->front;
    }
    free(cursor);
}

SceneLights *scene_lights_create(const Scene *scene) {
    if (!scene) return NULL;
    SceneLights *lights = allocate(1, sizeof(*lights));
    lights->sector_count = scene->sector_count;
    lights->asset_count = scene->asset_count;
    lights->sectors = allocate(lights->sector_count, sizeof(*lights->sectors));
    lights->assets = allocate(lights->asset_count, sizeof(*lights->assets));
    build_sector_geometry(lights, scene);
    build_neighbors(lights, scene);
    for (size_t index = 0; index < lights->asset_count; ++index) {
        const LightRule *rule = asset_rule(&scene->assets[index]);
        if (!rule) continue;
        AssetLight *light = &lights->assets[index];
        light->power = rule->power;
        light->radius = rule->radius;
        light->fullbright_only = rule->fullbright_only;
        light->valid = asset_color(scene, &scene->assets[index], light->color, &light->height);
    }
    return lights;
}

static float sector_radius(const SectorGeometry *geometry) {
    return fminf(14.0f, fmaxf(3.0f, 2.0f + 0.65f * sqrtf((float)geometry->area)));
}

static bool sector_source(const SceneLights *lights, const Scene *scene, size_t sector_index,
                          RayLight *output) {
    const SceneSector *sector = &scene->sectors[sector_index];
    const SectorGeometry *geometry = &lights->sectors[sector_index];
    if (geometry->area <= 0 || sector->ceiling_asset == UINT32_MAX || sector->ceiling <= sector->floor)
        return false;
    double neighbor_sum = 0;
    size_t neighbor_count = 0;
    for (size_t index = lights->neighbor_offsets[sector_index];
         index < lights->neighbor_offsets[sector_index + 1]; ++index) {
        const SceneSector *neighbor = &scene->sectors[lights->neighbors[index]];
        if (neighbor->ceiling <= neighbor->floor) continue;
        if (fminf(sector->ceiling, neighbor->ceiling) <= fmaxf(sector->floor, neighbor->floor)) continue;
        neighbor_sum += neighbor->light;
        ++neighbor_count;
    }
    if (!neighbor_count) return false;
    float contrast = (float)sector->light - (float)(neighbor_sum / (double)neighbor_count);
    if (contrast < 24.0f) return false;
    float power = fminf(4.0f, contrast * (8.0f / 255.0f));
    *output = (RayLight){
        .position = {geometry->x, sector->ceiling - 0.15f, geometry->z, sector_radius(geometry)},
        .color = {1.0f, 0.96f, 0.88f, power},
        .meta = {1, 0, 0, 0}
    };
    return true;
}

static float light_rank(const RayLight *light, Position camera) {
    float dx = light->position[0] - camera.x;
    float dy = light->position[1] - camera.y;
    float dz = light->position[2] - camera.z;
    float radius = light->position[3];
    float relative_distance = (dx * dx + dy * dy + dz * dz) / (radius * radius);
    return light->color[3] / (1.0f + relative_distance);
}

static void consider_light(RankedLight selected[MAX_RAY_LIGHTS], unsigned *count,
                           const RayLight *light, Position camera) {
    float rank = light_rank(light, camera);
    unsigned slot = 0;
    while (slot < *count && selected[slot].rank >= rank) ++slot;
    if (slot == MAX_RAY_LIGHTS) return;
    unsigned next_count = *count < MAX_RAY_LIGHTS ? *count + 1 : *count;
    for (unsigned index = next_count - 1; index > slot; --index) selected[index] = selected[index - 1];
    selected[slot] = (RankedLight){.light = *light, .rank = rank};
    *count = next_count;
}

unsigned scene_lights_collect(SceneLights *lights, const Scene *scene, Position camera,
                              RayLight output[MAX_RAY_LIGHTS]) {
    if (!lights || !scene || !output || lights->sector_count != scene->sector_count ||
        lights->asset_count != scene->asset_count) return 0;
    memset(output, 0, sizeof(*output) * MAX_RAY_LIGHTS);
    RankedLight selected[MAX_RAY_LIGHTS] = {0};
    unsigned count = 0;
    for (size_t index = 0; index < scene->sector_count; ++index) {
        RayLight light;
        if (sector_source(lights, scene, index, &light))
            consider_light(selected, &count, &light, camera);
    }
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        const Sprite *sprite = &scene->sprites[index];
        if (sprite->hidden || sprite->asset >= lights->asset_count || index >= UINT32_MAX) continue;
        const AssetLight *asset_light = &lights->assets[sprite->asset];
        if (!asset_light->valid || (asset_light->fullbright_only && !sprite->fullbright)) continue;
        float center_y = sprite->position.y + asset_light->height;
        RayLight light = {
            .position = {sprite->position.x, center_y, sprite->position.z, asset_light->radius},
            .color = {asset_light->color[0], asset_light->color[1], asset_light->color[2], asset_light->power},
            .meta = {0, (uint32_t)index + 1, 0, 0}
        };
        consider_light(selected, &count, &light, camera);
    }
    for (unsigned index = 0; index < count; ++index) output[index] = selected[index].light;
    return count;
}

void scene_lights_destroy(SceneLights *lights) {
    if (!lights) return;
    free(lights->sectors);
    free(lights->assets);
    free(lights->neighbor_offsets);
    free(lights->neighbors);
    free(lights);
}
