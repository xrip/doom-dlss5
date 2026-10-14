#include "scene.h"

#include <math.h>

void wall_vertices(const Scene *scene, const WallSurface *wall, SceneVertex vertices[6]) {
    const CollisionLine *line = &scene->lines[wall->line];
    uint32_t own = wall->side ? line->back : line->front;
    uint32_t other = wall->side ? line->front : line->back;
    const SceneSector *front = &scene->sectors[own];
    const SceneSector *back = other == UINT32_MAX ? NULL : &scene->sectors[other];
    const Asset *asset = &scene->assets[scene->batches[wall->batch].asset];
    MapPoint start = wall->side ? line->end : line->start, end = wall->side ? line->start : line->end;
    float bottom = front->floor, top = front->ceiling, anchor = top;
    float height = (float)asset->height / 32, offset_y = wall->offset_y / 32;
    if (wall->kind == WALL_UPPER && back) {
        bottom = fmaxf(back->ceiling, front->floor);
        anchor = line->flags & 8 ? front->ceiling : back->ceiling + height;
    } else if (wall->kind == WALL_LOWER && back) {
        top = fminf(back->floor, front->ceiling);
        anchor = line->flags & 16 ? front->ceiling : back->floor;
    } else if (back) {
        bottom = fmaxf(front->floor, back->floor); top = fminf(front->ceiling, back->ceiling);
        anchor = (line->flags & 16 ? bottom + height : top) + offset_y;
        top = fminf(top, anchor); bottom = fmaxf(bottom, anchor - height); offset_y = 0;
    } else anchor = line->flags & 16 ? front->floor + height : front->ceiling;
    if (top < bottom) top = bottom;
    float u0 = wall->offset_x / (float)asset->width;
    float u1 = u0 + hypotf(end.x - start.x, end.z - start.z) * 32 / (float)asset->width;
    float v0 = (anchor - bottom + offset_y) / height, v1 = (anchor - top + offset_y) / height;
    SceneVertex corners[4] = {{start.x, bottom, start.z, u0, v0}, {end.x, bottom, end.z, u1, v0},
                             {end.x, top, end.z, u1, v1}, {start.x, top, start.z, u0, v1}};
    const unsigned order[6] = {0, 1, 2, 0, 2, 3};
    for (unsigned index = 0; index < 6; ++index) vertices[index] = corners[order[index]];
}

float line_distance_squared(const CollisionLine *line, float x, float z) {
    float dx = line->end.x - line->start.x, dz = line->end.z - line->start.z;
    float fraction = ((x - line->start.x) * dx + (z - line->start.z) * dz) / (dx * dx + dz * dz);
    fraction = fmaxf(0, fminf(1, fraction));
    float offset_x = x - line->start.x - fraction * dx;
    float offset_z = z - line->start.z - fraction * dz;
    return offset_x * offset_x + offset_z * offset_z;
}
