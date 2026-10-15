#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct { float x, y, z; } Position;
typedef struct { float x, y, z, u, v; } SceneVertex;

typedef struct {
    size_t count;
    SceneVertex *vertices;
    float spin;
} VoxelMesh;
enum { SURFACE_WALL, SURFACE_FLOOR, SURFACE_CEILING, SURFACE_SWITCH };
enum { WALL_MIDDLE, WALL_UPPER, WALL_LOWER };
typedef struct {
    char name[9];
    uint32_t width, height;
    int32_t left, top;
    uint8_t *pixels;
    VoxelMesh voxel;
} Asset;
typedef struct {
    uint32_t asset, shade, sky;
    uint32_t owner, plane;
    size_t count, capacity;
    SceneVertex *vertices;
} Batch;
typedef struct {
    uint32_t asset, ticks;
    bool fullbright;
} SpriteFrame;

enum { MAX_SPRITE_FRAMES = 8 };

typedef struct {
    uint32_t asset, sector;
    Position position;
    float yaw;
    bool ceiling, fullbright, hidden;
    uint32_t frame_count, frame_index, frame_ticks;
    SpriteFrame frames[MAX_SPRITE_FRAMES];
} Sprite;
typedef struct { float x, z; } MapPoint;
typedef struct {
    float floor, ceiling;
    uint32_t special, tag;
    uint32_t light, floor_asset, ceiling_asset;
} SceneSector;

typedef struct { Position position; float yaw; uint32_t sector; } Teleport;
typedef struct {
    MapPoint start, end;
    uint32_t front, back, flags, special, tag;
} CollisionLine;
typedef struct {
    MapPoint points[3];
    uint32_t sector;
} CollisionTriangle;
typedef struct {
    uint32_t batch, first, line, side, kind, switch_asset;
    float offset_x, offset_y;
} WallSurface;
typedef struct {
    char map[9];
    Position spawn;
    float yaw;
    uint8_t lut[32 * 256 * 4];
    size_t asset_count, batch_count, sprite_count;
    Asset *assets;
    Batch *batches;
    Sprite *sprites;
    size_t sector_count, line_count, region_count;
    SceneSector *sectors;
    CollisionLine *lines;
    CollisionTriangle *regions;
    size_t wall_count;
    WallSurface *walls;
    size_t teleport_count;
    Teleport *teleports;
    uint32_t voxel_asset;
} Scene;

_Noreturn void fail(const char *format, ...);
void *allocate(size_t count, size_t size);
uint8_t *read_file(const char *path, size_t *size);
void write_file(const char *path, const void *bytes, size_t size);
char *scene_sidecar(const char *path, const char *extension);
uint16_t read_u16(const uint8_t *data);
uint32_t read_u32(const uint8_t *data);
int16_t read_i16(const uint8_t *data);
void require_range(size_t size, size_t offset, size_t length);
void scene_write(const Scene *scene, const char *path);
Scene scene_read(const char *path);
void scene_write_voxels(const Scene *scene, const char *path);
void scene_read_voxels(Scene *scene, const char *path);
const VoxelMesh *scene_sprite_voxel(const Scene *scene, const Sprite *sprite);
float asset_floor_offset(const Asset *asset);
void scene_free(Scene *scene);
void scene_print(const Scene *scene);
void scene_tick_sprites(Scene *scene);
void wall_vertices(const Scene *scene, const WallSurface *wall, SceneVertex vertices[6]);
float line_distance_squared(const CollisionLine *line, float x, float z);
uint32_t line_neighbor(const CollisionLine *line, uint32_t sector);
