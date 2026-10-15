#pragma once

#include "scene.h"

enum { VOXELS_AUTO = -1, VOXELS_OFF, VOXELS_ON };

typedef struct {
    const char *wad;
    char *converter, *directory, *voxel_root;
} LevelSource;

bool level_is_wad(const char *path);
LevelSource level_source_create(const char *wad, int voxel_mode);
char *level_source_prepare(const LevelSource *source, const char *map);
void level_source_free(LevelSource *source);
