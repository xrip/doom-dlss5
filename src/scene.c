#include "scene.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert(sizeof(float) == 4, "Scene files need 32-bit floats");

_Noreturn void fail(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    fputs("Error: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    exit(EXIT_FAILURE);
}

void *allocate(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) fail("Allocation overflow");
    void *result = calloc(count ? count : 1, size ? size : 1);
    if (!result) fail("Out of memory");
    return result;
}

uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8));
}

uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

int16_t read_i16(const uint8_t *data) {
    uint16_t value = read_u16(data);
    return (int16_t)(value <= INT16_MAX ? (int32_t)value : (int32_t)value - 65536);
}

void require_range(size_t size, size_t offset, size_t length) {
    if (offset > size || length > size - offset) fail("Invalid data range");
}

uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) fail("Cannot open %s", path);
    if (fseek(file, 0, SEEK_END)) fail("Cannot seek %s", path);
    long length = ftell(file);
    if (length < 0 || length > 512 * 1024 * 1024) fail("Invalid file size: %s", path);
    rewind(file);
    *size = (size_t)length;
    uint8_t *bytes = allocate(*size, 1);
    if (fread(bytes, 1, *size, file) != *size) fail("Cannot read %s", path);
    fclose(file);
    return bytes;
}

void write_file(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) fail("Cannot create %s", path);
    bool written = fwrite(bytes, 1, size, file) == size;
    bool closed = fclose(file) == 0;
    if (!written || !closed) fail("Cannot write %s", path);
}

char *scene_sidecar(const char *path, const char *extension) {
    size_t length = strlen(path), base = length;
    for (size_t index = length; index > 0; --index) {
        char character = path[index - 1];
        if (character == '/' || character == '\\') break;
        if (character == '.') { base = index - 1; break; }
    }
    char *result = allocate(base + strlen(extension) + 1, 1);
    memcpy(result, path, base);
    strcpy(result + base, extension);
    return result;
}

static void put_bytes(FILE *file, const void *bytes, size_t size) {
    if (fwrite(bytes, 1, size, file) != size) fail("Cannot write scene file");
}

static void put_u32(FILE *file, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    put_bytes(file, bytes, 4);
}

static void put_float(FILE *file, float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    put_u32(file, bits);
}

void scene_write(const Scene *scene, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) fail("Cannot create %s", path);
    put_bytes(file, "DRMAP006", 8);
    put_bytes(file, scene->map, 8);
    put_u32(file, (uint32_t)scene->asset_count);
    put_u32(file, (uint32_t)scene->batch_count);
    put_u32(file, (uint32_t)scene->sprite_count);
    put_float(file, scene->spawn.x);
    put_float(file, scene->spawn.y);
    put_float(file, scene->spawn.z);
    put_float(file, scene->yaw);
    put_bytes(file, scene->lut, sizeof(scene->lut));
    for (size_t index = 0; index < scene->asset_count; ++index) {
        const Asset *asset = &scene->assets[index];
        put_bytes(file, asset->name, 8);
        put_u32(file, asset->width);
        put_u32(file, asset->height);
        put_u32(file, (uint32_t)asset->left);
        put_u32(file, (uint32_t)asset->top);
        put_bytes(file, asset->pixels, (size_t)asset->width * asset->height * 4);
    }
    for (size_t index = 0; index < scene->batch_count; ++index) {
        const Batch *batch = &scene->batches[index];
        put_u32(file, batch->asset);
        put_u32(file, batch->shade);
        put_u32(file, batch->sky);
        put_u32(file, (uint32_t)batch->count);
        put_u32(file, batch->owner); put_u32(file, batch->plane);
        for (size_t vertex = 0; vertex < batch->count; ++vertex) {
            const SceneVertex *point = &batch->vertices[vertex];
            put_float(file, point->x); put_float(file, point->y); put_float(file, point->z);
            put_float(file, point->u); put_float(file, point->v);
        }
    }
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        const Sprite *sprite = &scene->sprites[index];
        put_u32(file, sprite->asset);
        put_u32(file, sprite->sector);
        put_u32(file, (uint32_t)sprite->ceiling | ((uint32_t)sprite->fullbright << 1));
        put_float(file, sprite->position.x);
        put_float(file, sprite->position.y);
        put_float(file, sprite->position.z);
        put_u32(file, sprite->frame_count);
        for (uint32_t frame = 0; frame < sprite->frame_count; ++frame) {
            put_u32(file, sprite->frames[frame].asset);
            put_u32(file, sprite->frames[frame].ticks);
            put_u32(file, (uint32_t)sprite->frames[frame].fullbright);
        }
    }
    put_u32(file, (uint32_t)scene->sector_count);
    put_u32(file, (uint32_t)scene->line_count);
    put_u32(file, (uint32_t)scene->region_count);
    for (size_t index = 0; index < scene->sector_count; ++index) {
        const SceneSector *sector = &scene->sectors[index];
        put_float(file, sector->floor); put_float(file, sector->ceiling);
        put_u32(file, sector->special); put_u32(file, sector->tag);
        put_u32(file, sector->light); put_u32(file, sector->floor_asset); put_u32(file, sector->ceiling_asset);
    }
    for (size_t index = 0; index < scene->line_count; ++index) {
        const CollisionLine *line = &scene->lines[index];
        put_float(file, line->start.x); put_float(file, line->start.z);
        put_float(file, line->end.x); put_float(file, line->end.z);
        put_u32(file, line->front); put_u32(file, line->back);
        put_u32(file, line->flags); put_u32(file, line->special); put_u32(file, line->tag);
    }
    for (size_t index = 0; index < scene->region_count; ++index) {
        const CollisionTriangle *region = &scene->regions[index];
        for (unsigned corner = 0; corner < 3; ++corner) {
            put_float(file, region->points[corner].x); put_float(file, region->points[corner].z);
        }
        put_u32(file, region->sector);
    }
    put_u32(file, (uint32_t)scene->wall_count);
    for (size_t index = 0; index < scene->wall_count; ++index) {
        const WallSurface *wall = &scene->walls[index];
        put_u32(file, wall->batch); put_u32(file, wall->first); put_u32(file, wall->line);
        put_u32(file, wall->side); put_u32(file, wall->kind); put_u32(file, wall->switch_asset);
        put_float(file, wall->offset_x); put_float(file, wall->offset_y);
    }
    put_u32(file, (uint32_t)scene->teleport_count);
    for (size_t index = 0; index < scene->teleport_count; ++index) {
        const Teleport *teleport = &scene->teleports[index];
        put_float(file, teleport->position.x); put_float(file, teleport->position.y); put_float(file, teleport->position.z);
        put_float(file, teleport->yaw); put_u32(file, teleport->sector);
    }
    if (fclose(file)) fail("Cannot finish scene file");
}

typedef struct { const uint8_t *bytes; size_t size, offset; } Reader;

static const uint8_t *take(Reader *reader, size_t size) {
    require_range(reader->size, reader->offset, size);
    const uint8_t *result = reader->bytes + reader->offset;
    reader->offset += size;
    return result;
}

static uint32_t get_u32(Reader *reader) { return read_u32(take(reader, 4)); }

static float get_float(Reader *reader) {
    uint32_t bits = get_u32(reader);
    float value;
    memcpy(&value, &bits, 4);
    if (!isfinite(value) || fabsf(value) > 1e7f) fail("Invalid scene coordinate");
    return value;
}

static Position get_position(Reader *reader) {
    Position position;
    position.x = get_float(reader);
    position.y = get_float(reader);
    position.z = get_float(reader);
    return position;
}

static int32_t get_signed(Reader *reader) {
    uint32_t bits = get_u32(reader);
    return bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - 4294967296LL);
}

Scene scene_read(const char *path) {
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    Reader reader = {bytes, size, 0};
    if (memcmp(take(&reader, 8), "DRMAP006", 8)) fail("Unsupported scene format; run wad-convert again");
    Scene scene = {0};
    memcpy(scene.map, take(&reader, 8), 8);
    scene.asset_count = get_u32(&reader);
    scene.batch_count = get_u32(&reader);
    scene.sprite_count = get_u32(&reader);
    if (scene.asset_count > 4096 || scene.batch_count > 65536 || scene.sprite_count > 65536)
        fail("Scene count limit exceeded");
    scene.spawn = get_position(&reader);
    scene.yaw = get_float(&reader);
    memcpy(scene.lut, take(&reader, sizeof(scene.lut)), sizeof(scene.lut));
    scene.assets = allocate(scene.asset_count, sizeof(Asset));
    scene.batches = allocate(scene.batch_count, sizeof(Batch));
    scene.sprites = allocate(scene.sprite_count, sizeof(Sprite));
    for (size_t index = 0; index < scene.asset_count; ++index) {
        Asset *asset = &scene.assets[index];
        memcpy(asset->name, take(&reader, 8), 8);
        asset->width = get_u32(&reader); asset->height = get_u32(&reader);
        asset->left = get_signed(&reader); asset->top = get_signed(&reader);
        if (!asset->width || !asset->height || asset->width > 4096 || asset->height > 4096 ||
            asset->left < -8192 || asset->left > 8192 || asset->top < -8192 || asset->top > 8192)
            fail("Invalid scene texture");
        size_t pixel_size = (size_t)asset->width * asset->height * 4;
        const uint8_t *pixels = take(&reader, pixel_size);
        asset->pixels = allocate(pixel_size, 1);
        memcpy(asset->pixels, pixels, pixel_size);
    }
    for (size_t index = 0; index < scene.batch_count; ++index) {
        Batch *batch = &scene.batches[index];
        batch->asset = get_u32(&reader); batch->shade = get_u32(&reader);
        batch->sky = get_u32(&reader); batch->count = get_u32(&reader);
        batch->owner = get_u32(&reader); batch->plane = get_u32(&reader);
        if (batch->asset >= scene.asset_count || batch->shade > 31 || batch->sky > 1 ||
            !batch->count || batch->count % 3 || batch->count > INT_MAX / 5)
            fail("Invalid scene mesh");
        require_range(reader.size, reader.offset, batch->count * 20);
        batch->vertices = allocate(batch->count, sizeof(SceneVertex));
        for (size_t vertex = 0; vertex < batch->count; ++vertex) {
            SceneVertex *point = &batch->vertices[vertex];
            point->x = get_float(&reader); point->y = get_float(&reader); point->z = get_float(&reader);
            point->u = get_float(&reader); point->v = get_float(&reader);
        }
    }
    for (size_t index = 0; index < scene.sprite_count; ++index) {
        Sprite *sprite = &scene.sprites[index];
        sprite->asset = get_u32(&reader);
        sprite->sector = get_u32(&reader);
        uint32_t flags = get_u32(&reader);
        if (flags > 3) fail("Invalid sprite flags");
        sprite->ceiling = (flags & 1) != 0; sprite->fullbright = (flags & 2) != 0;
        if (sprite->asset >= scene.asset_count) fail("Invalid scene sprite");
        sprite->position = get_position(&reader);
        sprite->frame_count = get_u32(&reader);
        if (sprite->frame_count > MAX_SPRITE_FRAMES) fail("Too many sprite animation frames");
        for (uint32_t frame = 0; frame < sprite->frame_count; ++frame) {
            SpriteFrame *entry = &sprite->frames[frame];
            entry->asset = get_u32(&reader);
            entry->ticks = get_u32(&reader);
            uint32_t bright = get_u32(&reader);
            if (entry->asset >= scene.asset_count || !entry->ticks || entry->ticks > 350 || bright > 1)
                fail("Invalid sprite animation frame");
            entry->fullbright = bright != 0;
        }
        if (sprite->frame_count) {
            sprite->asset = sprite->frames[0].asset;
            sprite->fullbright = sprite->frames[0].fullbright;
            sprite->frame_ticks = sprite->frames[0].ticks;
        }
    }
    scene.sector_count = get_u32(&reader);
    scene.line_count = get_u32(&reader);
    scene.region_count = get_u32(&reader);
    if (!scene.sector_count || scene.sector_count > 65536 || !scene.line_count ||
        scene.line_count > 65536 || !scene.region_count || scene.region_count > 1000000)
        fail("Invalid collision counts");
    require_range(reader.size, reader.offset, scene.sector_count * 16 + scene.line_count * 36 + scene.region_count * 28);
    scene.sectors = allocate(scene.sector_count, sizeof(SceneSector));
    scene.lines = allocate(scene.line_count, sizeof(CollisionLine));
    scene.regions = allocate(scene.region_count, sizeof(CollisionTriangle));
    for (size_t index = 0; index < scene.sprite_count; ++index)
        if (scene.sprites[index].sector >= scene.sector_count) fail("Invalid sprite sector");
    for (size_t index = 0; index < scene.sector_count; ++index) {
        SceneSector *sector = &scene.sectors[index];
        sector->floor = get_float(&reader); sector->ceiling = get_float(&reader);
        sector->special = get_u32(&reader); sector->tag = get_u32(&reader);
        sector->light = get_u32(&reader); sector->floor_asset = get_u32(&reader); sector->ceiling_asset = get_u32(&reader);
        if (sector->light > 255 || sector->floor_asset >= scene.asset_count ||
            (sector->ceiling_asset != UINT32_MAX && sector->ceiling_asset >= scene.asset_count)) fail("Invalid sector material");
        if (sector->floor > sector->ceiling) fail("Invalid collision sector heights");
    }
    for (size_t index = 0; index < scene.line_count; ++index) {
        CollisionLine *line = &scene.lines[index];
        line->start.x = get_float(&reader); line->start.z = get_float(&reader);
        line->end.x = get_float(&reader); line->end.z = get_float(&reader);
        line->front = get_u32(&reader); line->back = get_u32(&reader);
        line->flags = get_u32(&reader); line->special = get_u32(&reader); line->tag = get_u32(&reader);
        if ((line->front != UINT32_MAX && line->front >= scene.sector_count) ||
            (line->back != UINT32_MAX && line->back >= scene.sector_count) ||
            (line->front == UINT32_MAX && line->back == UINT32_MAX) ||
            (line->start.x == line->end.x && line->start.z == line->end.z)) fail("Invalid collision line");
    }
    for (size_t index = 0; index < scene.region_count; ++index) {
        CollisionTriangle *region = &scene.regions[index];
        for (unsigned corner = 0; corner < 3; ++corner) {
            region->points[corner].x = get_float(&reader); region->points[corner].z = get_float(&reader);
        }
        region->sector = get_u32(&reader);
        MapPoint first = region->points[0], second = region->points[1], third = region->points[2];
        double area = ((double)second.x - first.x) * ((double)third.z - first.z) -
                      ((double)second.z - first.z) * ((double)third.x - first.x);
        if (region->sector >= scene.sector_count || fabs(area) < 1e-10) fail("Invalid collision region");
    }
    for (size_t index = 0; index < scene.batch_count; ++index) {
        Batch *batch = &scene.batches[index];
        if (batch->plane > SURFACE_SWITCH ||
            ((batch->plane == SURFACE_FLOOR || batch->plane == SURFACE_CEILING) && batch->owner >= scene.sector_count) ||
            (batch->plane == SURFACE_SWITCH && batch->owner >= scene.line_count)) fail("Invalid surface owner");
    }
    scene.wall_count = get_u32(&reader);
    if (scene.wall_count > 400000) fail("Invalid wall surface count");
    require_range(reader.size, reader.offset, scene.wall_count * 32);
    scene.walls = allocate(scene.wall_count, sizeof(WallSurface));
    for (size_t index = 0; index < scene.wall_count; ++index) {
        WallSurface *wall = &scene.walls[index];
        wall->batch = get_u32(&reader); wall->first = get_u32(&reader); wall->line = get_u32(&reader);
        wall->side = get_u32(&reader); wall->kind = get_u32(&reader); wall->switch_asset = get_u32(&reader);
        wall->offset_x = get_float(&reader); wall->offset_y = get_float(&reader);
        if (wall->batch >= scene.batch_count || wall->line >= scene.line_count || wall->side > 1 || wall->kind > WALL_LOWER ||
            (wall->switch_asset != UINT32_MAX && wall->switch_asset >= scene.asset_count)) fail("Invalid wall surface");
        const Batch *batch = &scene.batches[wall->batch];
        const CollisionLine *line = &scene.lines[wall->line];
        if (wall->first > batch->count || batch->count - wall->first < 6 ||
            (wall->side ? line->back : line->front) == UINT32_MAX) fail("Invalid wall surface range");
    }
    scene.teleport_count = get_u32(&reader);
    if (scene.teleport_count > 65536) fail("Too many teleport destinations");
    scene.teleports = allocate(scene.teleport_count, sizeof(Teleport));
    for (size_t index = 0; index < scene.teleport_count; ++index) {
        Teleport *teleport = &scene.teleports[index];
        teleport->position = get_position(&reader); teleport->yaw = get_float(&reader); teleport->sector = get_u32(&reader);
        if (teleport->sector >= scene.sector_count) fail("Invalid teleport sector");
    }
    if (reader.offset != reader.size) fail("Unexpected trailing scene data");
    free(bytes);
    return scene;
}

void scene_write_voxels(const Scene *scene, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) fail("Cannot write %s", path);
    put_bytes(file, "DRVOX001", 8);
    put_bytes(file, scene->map, 8);
    put_u32(file, (uint32_t)scene->asset_count);
    put_u32(file, (uint32_t)scene->sprite_count);
    for (size_t index = 0; index < scene->asset_count; ++index) {
        const Asset *asset = &scene->assets[index];
        put_bytes(file, asset->name, 8);
        put_u32(file, (uint32_t)asset->voxel.count);
        put_float(file, asset->voxel.spin);
        for (size_t vertex = 0; vertex < asset->voxel.count; ++vertex) {
            const SceneVertex *point = &asset->voxel.vertices[vertex];
            put_float(file, point->x); put_float(file, point->y); put_float(file, point->z);
            put_float(file, point->u); put_float(file, point->v);
        }
    }
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        put_u32(file, scene->sprites[index].asset);
        put_float(file, scene->sprites[index].yaw);
    }
    if (fclose(file)) fail("Cannot finish %s", path);
}

void scene_read_voxels(Scene *scene, const char *path) {
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    Reader reader = {.bytes = bytes, .size = size};
    if (memcmp(take(&reader, 8), "DRVOX001", 8) || memcmp(take(&reader, 8), scene->map, 8) ||
        get_u32(&reader) != scene->asset_count || get_u32(&reader) != scene->sprite_count)
        fail("Voxel pack does not match this scene; run wad-convert again");
    size_t total = 0;
    for (size_t index = 0; index < scene->asset_count; ++index) {
        Asset *asset = &scene->assets[index];
        if (memcmp(take(&reader, 8), asset->name, 8)) fail("Voxel pack asset mismatch");
        VoxelMesh *mesh = &asset->voxel;
        mesh->count = get_u32(&reader);
        mesh->spin = get_float(&reader);
        if (mesh->count % 3 || mesh->count > 8000000 - total || fabsf(mesh->spin) > 3600)
            fail("Invalid voxel mesh size or spin");
        total += mesh->count;
        require_range(reader.size, reader.offset, mesh->count * 20);
        mesh->vertices = allocate(mesh->count, sizeof(*mesh->vertices));
        for (size_t vertex = 0; vertex < mesh->count; ++vertex) {
            SceneVertex *point = &mesh->vertices[vertex];
            point->x = get_float(&reader); point->y = get_float(&reader); point->z = get_float(&reader);
            point->u = get_float(&reader); point->v = get_float(&reader);
            if (point->u < 0 || point->u > 1 || point->v != 0.5f)
                fail("Invalid voxel palette coordinates");
        }
    }
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        if (get_u32(&reader) != scene->sprites[index].asset) fail("Voxel pack sprite mismatch");
        scene->sprites[index].yaw = get_float(&reader);
    }
    if (reader.offset != reader.size) fail("Unexpected trailing voxel data");
    free(bytes);
    if (scene->asset_count >= 4096) fail("No texture slot for voxel palette");
    Asset *assets = realloc(scene->assets, (scene->asset_count + 1) * sizeof(*assets));
    if (!assets) fail("Out of memory");
    scene->assets = assets;
    scene->voxel_asset = (uint32_t)scene->asset_count++;
    Asset *palette = &scene->assets[scene->voxel_asset];
    *palette = (Asset){.name = "VOXELPAL", .width = 256, .height = 1,
        .pixels = allocate(256, 4)};
    for (unsigned index = 0; index < 256; ++index) {
        palette->pixels[index * 4] = (uint8_t)index;
        palette->pixels[index * 4 + 3] = 255;
    }
}

float asset_floor_offset(const Asset *asset) {
    for (uint32_t row = asset->height; row > 0; --row) {
        for (uint32_t column = 0; column < asset->width; ++column)
            if (asset->pixels[((size_t)(row - 1) * asset->width + column) * 4 + 3])
                return ((float)row - (float)asset->top) / 32;
    }
    return 0;
}

const VoxelMesh *scene_sprite_voxel(const Scene *scene, const Sprite *sprite) {
    if (sprite->asset >= scene->asset_count) return NULL;
    const VoxelMesh *mesh = &scene->assets[sprite->asset].voxel;
    if (!mesh->count) return NULL;
    for (uint32_t frame = 0; frame < sprite->frame_count; ++frame)
        if (sprite->frames[frame].asset >= scene->asset_count ||
            !scene->assets[sprite->frames[frame].asset].voxel.count) return NULL;
    return mesh;
}

void scene_free(Scene *scene) {
    for (size_t index = 0; index < scene->asset_count; ++index) {
        free(scene->assets[index].pixels);
        free(scene->assets[index].voxel.vertices);
    }
    for (size_t index = 0; index < scene->batch_count; ++index) free(scene->batches[index].vertices);
    free(scene->assets); free(scene->batches); free(scene->sprites);
    free(scene->sectors); free(scene->lines); free(scene->regions);
    free(scene->walls); free(scene->teleports);
    *scene = (Scene){0};
}

void scene_tick_sprites(Scene *scene) {
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        Sprite *sprite = &scene->sprites[index];
        if (!sprite->hidden) {
            const VoxelMesh *mesh = scene_sprite_voxel(scene, sprite);
            if (mesh) sprite->yaw = remainderf(sprite->yaw - mesh->spin * (0.01745329252f / 35.0f), 6.283185307f);
        }
        if (!sprite->frame_count || sprite->hidden) continue;
        if (sprite->frame_ticks > 1) {
            --sprite->frame_ticks;
            continue;
        }
        sprite->frame_index = (sprite->frame_index + 1) % sprite->frame_count;
        const SpriteFrame *frame = &sprite->frames[sprite->frame_index];
        sprite->frame_ticks = frame->ticks;
        sprite->asset = frame->asset;
        sprite->fullbright = frame->fullbright;
    }
}

void scene_print(const Scene *scene) {
    size_t triangles = 0;
    for (size_t index = 0; index < scene->batch_count; ++index) triangles += scene->batches[index].count / 3;
    printf("%s: %zu textures, %zu batches, %zu triangles, %zu sprites\n",
           scene->map, scene->asset_count, scene->batch_count, triangles, scene->sprite_count);
    printf("Collision: %zu sectors, %zu lines, %zu regions\n", scene->sector_count, scene->line_count, scene->region_count);
    size_t models = 0, instances = 0, model_vertices = 0;
    for (size_t index = 0; index < scene->asset_count; ++index) {
        models += scene->assets[index].voxel.count != 0;
        model_vertices += scene->assets[index].voxel.count;
    }
    for (size_t index = 0; index < scene->sprite_count; ++index)
        instances += scene_sprite_voxel(scene, &scene->sprites[index]) != NULL;
    if (models) printf("Voxels: %zu frame meshes, %zu vertices, %zu/%zu objects replaced\n",
        models, model_vertices, instances, scene->sprite_count);
}
