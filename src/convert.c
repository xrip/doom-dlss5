#include "scene.h"
#include "voxel_convert.h"
#include "tesselator.h"
#include "mus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char name[9]; const uint8_t *data; size_t size; } Lump;
typedef struct { uint8_t *bytes; Lump *lumps; size_t count; } Wad;
typedef struct { double x, y; } Point;
typedef struct { Point points[3]; } Triangle;
typedef struct {
    Wad wad;
    const Lump *vertices, *lines, *sides, *sectors, *segs, *leaves, *nodes, *things;
    Scene scene;
    double *sector_area;
    bool *bsp_sectors;
} Converter;

static bool named(const char *left, const char *right) {
    for (unsigned index = 0; index < 8; ++index) {
        unsigned char first = (unsigned char)left[index], second = (unsigned char)right[index];
        if (first >= 'a' && first <= 'z') first = (unsigned char)(first - 'a' + 'A');
        if (second >= 'a' && second <= 'z') second = (unsigned char)(second - 'a' + 'A');
        if (first != second) return false;
        if (!first) return true;
    }
    return true;
}

static Wad wad_read(const char *path) {
    Wad wad = {0};
    size_t size;
    wad.bytes = read_file(path, &size);
    require_range(size, 0, 12);
    if (memcmp(wad.bytes, "IWAD", 4) && memcmp(wad.bytes, "PWAD", 4)) fail("Invalid WAD header");
    wad.count = read_u32(wad.bytes + 4);
    size_t directory = read_u32(wad.bytes + 8);
    if (!wad.count || wad.count > 1000000) fail("Invalid WAD lump count");
    require_range(size, directory, wad.count * 16);
    wad.lumps = allocate(wad.count, sizeof(Lump));
    for (size_t index = 0; index < wad.count; ++index) {
        const uint8_t *entry = wad.bytes + directory + index * 16;
        size_t offset = read_u32(entry), length = read_u32(entry + 4);
        require_range(size, offset, length);
        Lump *lump = &wad.lumps[index];
        memcpy(lump->name, entry + 8, 8);
        lump->data = wad.bytes + offset;
        lump->size = length;
    }
    return wad;
}

static const Lump *find_lump(const Wad *wad, const char *name) {
    for (size_t index = wad->count; index > 0; --index)
        if (named(wad->lumps[index - 1].name, name)) return &wad->lumps[index - 1];
    fail("Missing WAD lump %.8s", name);
}

static const uint8_t *record(const Lump *lump, size_t index, size_t stride) {
    if (index >= lump->size / stride) fail("Invalid %.8s reference %zu", lump->name, index);
    return lump->data + index * stride;
}

static const Lump *map_lump(const Wad *wad, size_t marker, const char *name, size_t stride) {
    for (size_t index = marker + 1; index < wad->count && index <= marker + 10; ++index) {
        const Lump *lump = &wad->lumps[index];
        if (named(lump->name, name)) {
            if (lump->size % stride) fail("Bad %.8s record size", name);
            return lump;
        }
    }
    fail("Missing map lump %s", name);
}

static Point vertex(const Converter *converter, unsigned index) {
    const uint8_t *data = record(converter->vertices, index, 4);
    return (Point){read_i16(data), read_i16(data + 2)};
}

static const uint8_t *sector(const Converter *converter, unsigned index) {
    return record(converter->sectors, index, 26);
}

static unsigned line_sector(const Converter *converter, const uint8_t *line, unsigned side) {
    unsigned index = read_u16(line + 10 + side * 2);
    return read_u16(record(converter->sides, index, 30) + 28);
}

static unsigned shade(const uint8_t *sector_data) {
    int level = read_i16(sector_data + 20);
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    return (unsigned)((255 - level) * 31 / 255);
}

static double cross(Point start, Point direction, Point point) {
    return direction.x * (point.y - start.y) - direction.y * (point.x - start.x);
}

static double triangle_area(const Triangle *triangle) {
    Point start = triangle->points[0], end = triangle->points[1];
    return fabs(cross(start, (Point){end.x - start.x, end.y - start.y}, triangle->points[2])) * 0.5;
}

static Asset patch_decode(const Lump *lump) {
    require_range(lump->size, 0, 8);
    Asset asset = {0};
    memcpy(asset.name, lump->name, 8);
    asset.width = read_u16(lump->data); asset.height = read_u16(lump->data + 2);
    asset.left = read_i16(lump->data + 4); asset.top = read_i16(lump->data + 6);
    if (!asset.width || !asset.height || asset.width > 4096 || asset.height > 4096)
        fail("Invalid patch %.8s dimensions", lump->name);
    require_range(lump->size, 8, asset.width * 4);
    asset.pixels = allocate((size_t)asset.width * asset.height, 4);
    for (uint32_t column = 0; column < asset.width; ++column) {
        size_t offset = read_u32(lump->data + 8 + column * 4);
        int previous = -1;
        for (;;) {
            require_range(lump->size, offset, 1);
            unsigned delta = lump->data[offset];
            if (delta == 255) break;
            require_range(lump->size, offset, 4);
            unsigned length = lump->data[offset + 1];
            require_range(lump->size, offset, length + 4);
            int row = (int)delta;
            if (row <= previous) row += previous;
            previous = row;
            for (unsigned pixel = 0; pixel < length; ++pixel) {
                if ((unsigned)row + pixel >= asset.height) fail("Patch post outside %.8s", lump->name);
                uint8_t *target = asset.pixels + (((size_t)row + pixel) * asset.width + column) * 4;
                target[0] = lump->data[offset + 3 + pixel]; target[3] = 255;
            }
            offset += length + 4;
        }
    }
    return asset;
}

static Asset texture_decode(const Wad *wad, const char *name) {
    const Lump *names = find_lump(wad, "PNAMES");
    require_range(names->size, 0, 4);
    uint32_t patch_count = read_u32(names->data);
    require_range(names->size, 4, (size_t)patch_count * 8);
    const char *tables[2] = {"TEXTURE1", "TEXTURE2"};
    for (unsigned table_index = 0; table_index < 2; ++table_index) {
        const Lump *table = NULL;
        for (size_t index = 0; index < wad->count; ++index)
            if (named(wad->lumps[index].name, tables[table_index])) table = &wad->lumps[index];
        if (!table) continue;
        require_range(table->size, 0, 4);
        uint32_t count = read_u32(table->data);
        require_range(table->size, 4, (size_t)count * 4);
        for (uint32_t index = 0; index < count; ++index) {
            size_t offset = read_u32(table->data + 4 + index * 4);
            require_range(table->size, offset, 22);
            const uint8_t *description = table->data + offset;
            if (!named((const char *)description, name)) continue;
            Asset asset = {0};
            memcpy(asset.name, name, 8);
            asset.width = read_u16(description + 12); asset.height = read_u16(description + 14);
            if (!asset.width || !asset.height || asset.width > 4096 || asset.height > 4096)
                fail("Invalid texture %.8s dimensions", name);
            asset.pixels = allocate((size_t)asset.width * asset.height, 4);
            unsigned count_patches = read_u16(description + 20);
            require_range(table->size, offset + 22, count_patches * 10);
            for (unsigned patch_index = 0; patch_index < count_patches; ++patch_index) {
                const uint8_t *placement = description + 22 + patch_index * 10;
                int origin_x = read_i16(placement), origin_y = read_i16(placement + 2);
                unsigned name_index = read_u16(placement + 4);
                if (name_index >= patch_count) fail("Invalid PNAMES reference");
                char patch_name[9] = {0};
                memcpy(patch_name, names->data + 4 + name_index * 8, 8);
                Asset patch = patch_decode(find_lump(wad, patch_name));
                for (uint32_t row = 0; row < patch.height; ++row) {
                    for (uint32_t column = 0; column < patch.width; ++column) {
                        int target_x = origin_x + (int)column, target_y = origin_y + (int)row;
                        const uint8_t *source = patch.pixels + ((size_t)row * patch.width + column) * 4;
                        if (target_x >= 0 && target_y >= 0 && (uint32_t)target_x < asset.width &&
                            (uint32_t)target_y < asset.height && source[3])
                            memcpy(asset.pixels + ((size_t)target_y * asset.width + (unsigned)target_x) * 4, source, 4);
                    }
                }
                free(patch.pixels);
            }
            return asset;
        }
    }
    fail("Missing wall texture %.8s", name);
}

static uint32_t get_asset(Converter *converter, const char *name, unsigned kind) {
    char key[9] = {0};
    size_t key_length = strlen(name);
    if (key_length > 8) key_length = 8;
    memcpy(key, name, key_length);
    Scene *scene = &converter->scene;
    for (size_t index = 0; index < scene->asset_count; ++index)
        if (named(scene->assets[index].name, key)) return (uint32_t)index;
    Asset asset;
    if (kind == 0) asset = texture_decode(&converter->wad, key);
    else if (kind == 1) {
        const Lump *flat = find_lump(&converter->wad, key);
        if (flat->size != 4096) fail("Invalid flat %.8s", key);
        asset = (Asset){.width = 64, .height = 64};
        memcpy(asset.name, key, 8);
        asset.pixels = allocate(4096, 4);
        for (size_t index = 0; index < 4096; ++index) {
            asset.pixels[index * 4] = flat->data[index]; asset.pixels[index * 4 + 3] = 255;
        }
    } else {
        const Lump *patch = NULL;
        for (size_t index = 0; index < converter->wad.count; ++index) {
            const Lump *candidate = &converter->wad.lumps[index];
            if (named(candidate->name, key) || (key_length == 6 && key[5] != '0' && !strncmp(candidate->name, key, 6))) {
                patch = candidate; break;
            }
        }
        if (!patch) fail("Missing sprite patch %s", key);
        asset = patch_decode(patch); memcpy(asset.name, key, 8);
    }
    if (scene->asset_count == 4096) fail("Too many scene assets");
    Asset *assets = realloc(scene->assets, (scene->asset_count + 1) * sizeof(Asset));
    if (!assets) fail("Out of memory");
    scene->assets = assets;
    scene->assets[scene->asset_count] = asset;
    return (uint32_t)scene->asset_count++;
}

static Batch *get_batch(Scene *scene, uint32_t asset, uint32_t level, bool sky, uint32_t owner, uint32_t plane) {
    for (size_t index = 0; index < scene->batch_count; ++index) {
        Batch *batch = &scene->batches[index];
        if (batch->asset == asset && batch->shade == level && batch->sky == (unsigned)sky &&
            batch->owner == owner && batch->plane == plane) return batch;
    }
    Batch *batches = realloc(scene->batches, (scene->batch_count + 1) * sizeof(Batch));
    if (!batches) fail("Out of memory");
    scene->batches = batches;
    Batch *batch = &batches[scene->batch_count++];
    *batch = (Batch){.asset = asset, .shade = level, .sky = sky, .owner = owner, .plane = plane};
    return batch;
}

static void append_vertex(Batch *batch, SceneVertex vertex_data) {
    if (batch->count == batch->capacity) {
        size_t capacity = batch->capacity ? batch->capacity * 2 : 192;
        if (capacity > 10000000) fail("Mesh vertex limit exceeded");
        SceneVertex *vertices = realloc(batch->vertices, capacity * sizeof(SceneVertex));
        if (!vertices) fail("Out of memory");
        batch->vertices = vertices; batch->capacity = capacity;
    }
    batch->vertices[batch->count++] = vertex_data;
}

static SceneVertex world_vertex(Point point, double height, double u, double v) {
    return (SceneVertex){(float)(point.x / 32), (float)(height / 32), (float)(-point.y / 32),
                         (float)u, (float)v};
}

static void flat_triangle(Converter *converter, const Triangle *triangle, unsigned sector_index, bool ceiling) {
    const uint8_t *data = sector(converter, sector_index);
    char name[9] = {0};
    memcpy(name, data + (ceiling ? 12 : 4), 8);
    bool sky = named(name, "F_SKY1");
    unsigned map_number = (unsigned)(converter->scene.map[3] - '0') * 10 + (unsigned)(converter->scene.map[4] - '0');
    const char *sky_name = map_number <= 11 ? "SKY1" : map_number <= 20 ? "SKY2" : "SKY3";
    uint32_t asset = get_asset(converter, sky ? sky_name : name, sky ? 0 : 1);
    Batch *batch = get_batch(&converter->scene, asset, shade(data), sky, sector_index,
                             ceiling ? SURFACE_CEILING : SURFACE_FLOOR);
    double height = read_i16(data + (ceiling ? 2 : 0));
    for (unsigned corner = 0; corner < 3; ++corner) {
        Point point = triangle->points[ceiling ? 2 - corner : corner];
        append_vertex(batch, world_vertex(point, height, point.x / 64, -point.y / 64));
    }
}

static void append_triangle(Converter *converter, const Triangle *triangle, unsigned sector_index) {
    double area = triangle_area(triangle);
    converter->sector_area[sector_index] += area;
    if (area < 1e-7) return;
    Scene *scene = &converter->scene;
    CollisionTriangle *region = &scene->regions[scene->region_count++];
    region->sector = sector_index;
    for (unsigned corner = 0; corner < 3; ++corner)
        region->points[corner] = (MapPoint){(float)(triangle->points[corner].x / 32),
                                          (float)(-triangle->points[corner].y / 32)};
    flat_triangle(converter, triangle, sector_index, false);
    flat_triangle(converter, triangle, sector_index, true);
}

typedef struct { unsigned from, to; bool used; } Edge;

static void make_flats(Converter *converter) {
    size_t max_edges = converter->lines->size / 14 * 2;
    Edge *edges = allocate(max_edges, sizeof(Edge));
    TESSreal *contour = allocate(max_edges * 2, sizeof(TESSreal));
    for (unsigned sector_index = 0; sector_index < converter->sectors->size / 26; ++sector_index) {
        size_t edge_count = 0;
        for (size_t index = 0; index < converter->lines->size / 14; ++index) {
            const uint8_t *line = record(converter->lines, index, 14);
            unsigned right = read_u16(line + 10) == 65535 ? UINT32_MAX : line_sector(converter, line, 0);
            unsigned left = read_u16(line + 12) == 65535 ? UINT32_MAX : line_sector(converter, line, 1);
            if (right == left) continue;
            if (right == sector_index) edges[edge_count++] = (Edge){read_u16(line), read_u16(line + 2), false};
            if (left == sector_index) edges[edge_count++] = (Edge){read_u16(line + 2), read_u16(line), false};
        }
        if (!edge_count) fail("Sector %u has no outline", sector_index);
        int *balance = allocate(converter->vertices->size / 4, sizeof(int));
        for (size_t index = 0; index < edge_count; ++index) {
            ++balance[edges[index].from]; --balance[edges[index].to];
        }
        bool open = false;
        for (size_t index = 0; index < converter->vertices->size / 4; ++index) open = open || balance[index] != 0;
        free(balance);
        if (open) { converter->bsp_sectors[sector_index] = true; continue; }
        TESStesselator *tess = tessNewTess(NULL);
        if (!tess) fail("Cannot create tessellator");
        for (size_t first = 0; first < edge_count; ++first) {
            if (edges[first].used) continue;
            size_t current = first, count = 0;
            unsigned start_vertex = edges[first].from;
            for (;;) {
                Edge *edge = &edges[current];
                if (edge->used) fail("Non-simple sector contour");
                edge->used = true;
                Point point = vertex(converter, edge->from);
                contour[count * 2] = (TESSreal)point.x; contour[count * 2 + 1] = (TESSreal)point.y;
                ++count;
                unsigned next_vertex = edge->to;
                if (next_vertex == start_vertex) break;
                size_t next = edge_count;
                double best_turn = 4;
                Point before = vertex(converter, edge->from), at = vertex(converter, edge->to);
                for (size_t candidate = 0; candidate < edge_count; ++candidate) {
                    if (!edges[candidate].used && edges[candidate].from == next_vertex) {
                        Point after = vertex(converter, edges[candidate].to);
                        double incoming_x = at.x - before.x, incoming_y = at.y - before.y;
                        double outgoing_x = after.x - at.x, outgoing_y = after.y - at.y;
                        double turn = atan2(incoming_x * outgoing_y - incoming_y * outgoing_x,
                                            incoming_x * outgoing_x + incoming_y * outgoing_y);
                        if (turn < best_turn) { best_turn = turn; next = candidate; }
                    }
                }
                if (next == edge_count) fail("Open sector outline");
                current = next;
            }
            if (count < 3) fail("Degenerate sector contour");
            tessAddContour(tess, 2, contour, (int)(sizeof(TESSreal) * 2), (int)count);
        }
        if (!tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 2, NULL)) fail("Sector triangulation failed");
        const TESSreal *points = tessGetVertices(tess);
        const TESSindex *triangles = tessGetElements(tess);
        Scene *scene = &converter->scene;
        size_t added = (size_t)tessGetElementCount(tess);
        CollisionTriangle *regions = realloc(scene->regions, (scene->region_count + added) * sizeof(CollisionTriangle));
        if (!regions) fail("Out of memory");
        scene->regions = regions;
        for (int index = 0; index < tessGetElementCount(tess); ++index) {
            Triangle triangle = {0};
            for (unsigned corner = 0; corner < 3; ++corner) {
                TESSindex point = triangles[index * 3 + (int)corner];
                if (point == TESS_UNDEF) fail("Incomplete triangulation");
                triangle.points[corner] = (Point){points[point * 2], points[point * 2 + 1]};
            }
            append_triangle(converter, &triangle, sector_index);
        }
        tessDeleteTess(tess);
    }
    free(edges); free(contour);
}

typedef struct { size_t count; Point points[128]; } ClipPolygon;

static ClipPolygon clip_polygon(const ClipPolygon *input, Point start, Point direction, bool left) {
    ClipPolygon output = {0};
    for (size_t index = 0; index < input->count; ++index) {
        Point first = input->points[index], second = input->points[(index + 1) % input->count];
        double sign = left ? 1 : -1;
        double from = cross(start, direction, first) * sign, to = cross(start, direction, second) * sign;
        bool from_inside = from >= -1e-8, to_inside = to >= -1e-8;
        if (from_inside) {
            if (output.count == 128) fail("BSP polygon limit exceeded");
            output.points[output.count++] = first;
        }
        if (from_inside != to_inside) {
            if (output.count == 128) fail("BSP polygon limit exceeded");
            double fraction = from / (from - to);
            output.points[output.count++] = (Point){first.x + (second.x - first.x) * fraction,
                                                    first.y + (second.y - first.y) * fraction};
        }
    }
    return output;
}

static void bsp_flats(Converter *converter, unsigned child, const ClipPolygon *input, unsigned depth) {
    if (depth > 128) fail("BSP depth limit exceeded");
    if (!(child & 0x8000)) {
        const uint8_t *node = record(converter->nodes, child, 28);
        Point start = {read_i16(node), read_i16(node + 2)}, direction = {read_i16(node + 4), read_i16(node + 6)};
        for (unsigned side = 0; side < 2; ++side) {
            ClipPolygon *polygon = allocate(1, sizeof(ClipPolygon));
            *polygon = clip_polygon(input, start, direction, side != 0);
            bsp_flats(converter, read_u16(node + 24 + side * 2), polygon, depth + 1);
            free(polygon);
        }
        return;
    }
    const uint8_t *leaf = record(converter->leaves, child & 0x7fff, 4);
    unsigned first = read_u16(leaf + 2), count = read_u16(leaf);
    const uint8_t *first_seg = record(converter->segs, first, 12);
    unsigned sector_index = line_sector(converter, record(converter->lines, read_u16(first_seg + 6), 14), read_u16(first_seg + 8));
    if (!converter->bsp_sectors[sector_index]) return;
    ClipPolygon polygon = *input;
    for (unsigned index = 0; index < count; ++index) {
        const uint8_t *seg = record(converter->segs, first + index, 12);
        Point start = vertex(converter, read_u16(seg)), end = vertex(converter, read_u16(seg + 2));
        if (start.x != end.x || start.y != end.y)
            polygon = clip_polygon(&polygon, start, (Point){end.x - start.x, end.y - start.y}, false);
    }
    Scene *scene = &converter->scene;
    size_t added = polygon.count >= 3 ? polygon.count - 2 : 0;
    CollisionTriangle *regions = realloc(scene->regions, (scene->region_count + added + 1) * sizeof(CollisionTriangle));
    if (!regions) fail("Out of memory");
    scene->regions = regions;
    for (size_t index = 1; index + 1 < polygon.count; ++index) {
        Triangle triangle = {.points = {polygon.points[0], polygon.points[index], polygon.points[index + 1]}};
        append_triangle(converter, &triangle, sector_index);
    }
}

static unsigned point_sector(const Converter *converter, Point point) {
    unsigned child = converter->nodes->size ? (unsigned)(converter->nodes->size / 28 - 1) : 0x8000;
    unsigned depth = 0;
    while (!(child & 0x8000)) {
        if (++depth > 128) fail("Invalid BSP point lookup");
        const uint8_t *node = record(converter->nodes, child, 28);
        Point start = {read_i16(node), read_i16(node + 2)};
        Point direction = {read_i16(node + 4), read_i16(node + 6)};
        unsigned side = cross(start, direction, point) > 0 ? 1 : 0;
        child = read_u16(node + 24 + side * 2);
    }
    const uint8_t *leaf = record(converter->leaves, child & 0x7fff, 4);
    const uint8_t *seg = record(converter->segs, read_u16(leaf + 2), 12);
    return line_sector(converter, record(converter->lines, read_u16(seg + 6), 14), read_u16(seg + 8));
}

static void wall(Converter *converter, unsigned line_index, unsigned side_index, unsigned kind,
                 const uint8_t *side, const char *name) {
    if (name[0] == '-') return;
    Scene *scene = &converter->scene;
    uint32_t asset = get_asset(converter, name, 0), switch_asset = UINT32_MAX;
    const CollisionLine *line = &scene->lines[line_index];
    bool is_switch = line->special && name[0] == 'S' && name[1] == 'W' && name[2] == '1';
    if (is_switch) {
        char alternate[9] = {0}; memcpy(alternate, name, 8); alternate[2] = '2';
        switch_asset = get_asset(converter, alternate, 0);
    }
    unsigned sector_index = side_index ? line->back : line->front;
    Batch *batch = get_batch(scene, asset, shade(sector(converter, sector_index)), false,
                             is_switch ? line_index : UINT32_MAX, is_switch ? SURFACE_SWITCH : SURFACE_WALL);
    WallSurface surface = {.batch = (uint32_t)(batch - scene->batches), .first = (uint32_t)batch->count,
        .line = line_index, .side = side_index, .kind = kind, .switch_asset = switch_asset,
        .offset_x = read_i16(side), .offset_y = read_i16(side + 2)};
    SceneVertex vertices[6]; wall_vertices(scene, &surface, vertices);
    for (unsigned index = 0; index < 6; ++index) append_vertex(batch, vertices[index]);
    WallSurface *walls = realloc(scene->walls, (scene->wall_count + 1) * sizeof(WallSurface));
    if (!walls) fail("Out of memory");
    scene->walls = walls; scene->walls[scene->wall_count++] = surface;
}

static void make_walls(Converter *converter) {
    for (size_t index = 0; index < converter->lines->size / 14; ++index) {
        const uint8_t *line = record(converter->lines, index, 14);
        Point from = vertex(converter, read_u16(line)), to = vertex(converter, read_u16(line + 2));
        if (from.x == to.x && from.y == to.y) fail("Zero length linedef");
        for (unsigned side_index = 0; side_index < 2; ++side_index) {
            unsigned own_index = read_u16(line + 10 + side_index * 2);
            unsigned other_index = read_u16(line + 10 + (1 - side_index) * 2);
            if (own_index == 65535) continue;
            const uint8_t *side = record(converter->sides, own_index, 30);
            const uint8_t *front = sector(converter, read_u16(side + 28));
            char upper[9] = {0}, lower[9] = {0}, middle[9] = {0};
            memcpy(upper, side + 4, 8); memcpy(lower, side + 12, 8); memcpy(middle, side + 20, 8);
            if (other_index == 65535) {
                wall(converter, (unsigned)index, side_index, WALL_MIDDLE, side, middle);
                continue;
            }
            const uint8_t *back = sector(converter, read_u16(record(converter->sides, other_index, 30) + 28));
            bool both_sky = named((const char *)front + 12, "F_SKY1") && named((const char *)back + 12, "F_SKY1");
            if (!both_sky) wall(converter, (unsigned)index, side_index, WALL_UPPER, side, upper);
            wall(converter, (unsigned)index, side_index, WALL_LOWER, side, lower);
            wall(converter, (unsigned)index, side_index, WALL_MIDDLE, side, middle);
        }
    }
}

typedef struct { unsigned type; const char *patch; unsigned flags; } ThingImage;
static const ThingImage thing_images[] = {
    {3001, "TROOA1", false}, {3004, "POSSA1", false},
    {2007, "CLIPA0", false}, {2015, "BON2A0", false}, {2011, "STIMA0", false},
    {2018, "ARM1A0", false}, {2014, "BON1A0", false}, {2001, "SHOTA0", false},
    {2008, "SHELA0", false}, {2012, "MEDIA0", false}, {2003, "LAUNA0", false},
    {82, "SGN2A0", false}, {2004, "PLASA0", false}, {2006, "BFUGA0", false},
    {2002, "MGUNA0", false}, {2046, "BROKA0", false}, {2005, "CSAWA0", false},
    {5, "BKEYA0", true}, {6, "YKEYA0", true}, {13, "RKEYA0", true},
    {38, "RSKUA0", true}, {39, "YSKUA0", true}, {40, "BSKUA0", true},
    {7, "SPIDA1", false}, {8, "BPAKA0", false}, {9, "SPOSA1", false},
    {16, "CYBRA1", false}, {17, "CELPA0", false}, {58, "SARGA1", false},
    {64, "VILEA1", false}, {65, "CPOSA1", false}, {66, "SKELA1", false},
    {67, "FATTA1", false}, {68, "BSPIA1", false}, {69, "BOS2A1", false},
    {71, "PAINA1", false}, {72, "KEENA0", 2}, {84, "SSWVA1", false},
    {88, "BBRNA0", false}, {3002, "SARGA1", false}, {3003, "BOSSA1", false},
    {3005, "HEADA1", false}, {3006, "SKULA1", true},
    {10, "PLAYW0", false}, {12, "PLAYW0", false}, {15, "PLAYN0", false},
    {18, "POSSL0", false}, {19, "SPOSL0", false}, {22, "HEADL0", false},
    {24, "POL5A0", false}, {25, "POL1A0", false}, {26, "POL6A0", false},
    {27, "POL4A0", false}, {28, "POL2A0", false}, {29, "POL3A0", true},
    {30, "COL1A0", false}, {31, "COL2A0", false}, {32, "COL3A0", false},
    {34, "CANDA0", true}, {35, "CBRAA0", true}, {36, "COL5A0", false},
    {37, "COL6A0", false}, {41, "CEYEA0", true}, {42, "FSKUA0", true},
    {43, "TRE1A0", false}, {44, "TBLUA0", true}, {45, "TGRNA0", true},
    {46, "TREDA0", true}, {47, "SMITA0", false}, {48, "ELECA0", false},
    {49, "GOR1A0", 2}, {50, "GOR2A0", 2},
    {51, "GOR3A0", 2}, {52, "GOR4A0", 2}, {53, "GOR5A0", 2},
    {54, "TRE2A0", false}, {55, "SMBTA0", true}, {56, "SMGTA0", true}, {57, "SMRTA0", true},
    {59, "GOR2A0", 2}, {60, "GOR4A0", 2},
    {61, "GOR3A0", 2}, {62, "GOR5A0", 2}, {63, "GOR1A0", 2},
    {70, "FCANA0", true}, {73, "HDB1A0", 2}, {74, "HDB2A0", 2},
    {75, "HDB3A0", 2}, {76, "HDB4A0", 2},
    {77, "HDB5A0", 2}, {78, "HDB6A0", 2},
    {79, "POB1A0", false}, {80, "POB2A0", false}, {81, "BRS1A0", false},
    {85, "TLMPA0", true}, {86, "TLP2A0", true},
    {83, "MEGAA0", true}, {2010, "ROCKA0", false}, {2013, "SOULA0", true},
    {2019, "ARM2A0", false}, {2022, "PINVA0", true}, {2023, "PSTRA0", false},
    {2024, "PINSA0", true}, {2025, "SUITA0", false}, {2026, "PMAPA0", true},
    {2035, "BAR1A0", false}, {2045, "PVISA0", true}, {2047, "CELLA0", false},
    {2048, "AMMOA0", false}, {2049, "SBOXA0", false}
};

typedef struct {
    const char *patch, *frames;
    unsigned ticks[MAX_SPRITE_FRAMES], bright;
} ThingAnimation;

static const ThingAnimation thing_animations[] = {
    {"TROOA1", "AB", {10, 10}, 0},
    {"POSSA1", "AB", {10, 10}, 0},
    {"SPOSA1", "AB", {10, 10}, 0},
    {"SPIDA1", "AB", {10, 10}, 0},
    {"CYBRA1", "AB", {10, 10}, 0},
    {"SARGA1", "AB", {10, 10}, 0},
    {"VILEA1", "AB", {10, 10}, 0},
    {"CPOSA1", "AB", {10, 10}, 0},
    {"SKELA1", "AB", {10, 10}, 0},
    {"FATTA1", "AB", {15, 15}, 0},
    {"BSPIA1", "AB", {10, 10}, 0},
    {"BOS2A1", "AB", {10, 10}, 0},
    {"SSWVA1", "AB", {10, 10}, 0},
    {"BOSSA1", "AB", {10, 10}, 0},
    {"SKULA1", "AB", {10, 10}, 3},
    {"BON1A0", "ABCDCB", {6, 6, 6, 6, 6, 6}, 0},
    {"BON2A0", "ABCDCB", {6, 6, 6, 6, 6, 6}, 0},
    {"ARM1A0", "AB", {6, 7}, 2},
    {"ARM2A0", "AB", {6, 6}, 2},
    {"BKEYA0", "AB", {10, 10}, 2},
    {"YKEYA0", "AB", {10, 10}, 2},
    {"RKEYA0", "AB", {10, 10}, 2},
    {"RSKUA0", "AB", {10, 10}, 2},
    {"YSKUA0", "AB", {10, 10}, 2},
    {"BSKUA0", "AB", {10, 10}, 2},
    {"POL6A0", "AB", {6, 8}, 0},
    {"POL3A0", "AB", {6, 6}, 3},
    {"COL5A0", "AB", {14, 14}, 0},
    {"CEYEA0", "ABCB", {6, 6, 6, 6}, 15},
    {"FSKUA0", "ABC", {6, 6, 6}, 7},
    {"TBLUA0", "ABCD", {4, 4, 4, 4}, 15},
    {"TGRNA0", "ABCD", {4, 4, 4, 4}, 15},
    {"TREDA0", "ABCD", {4, 4, 4, 4}, 15},
    {"GOR1A0", "ABCB", {10, 15, 8, 6}, 0},
    {"SMBTA0", "ABCD", {4, 4, 4, 4}, 15},
    {"SMGTA0", "ABCD", {4, 4, 4, 4}, 15},
    {"SMRTA0", "ABCD", {4, 4, 4, 4}, 15},
    {"FCANA0", "ABC", {4, 4, 4}, 7},
    {"TLMPA0", "ABCD", {4, 4, 4, 4}, 15},
    {"TLP2A0", "ABCD", {4, 4, 4, 4}, 15},
    {"MEGAA0", "ABCD", {6, 6, 6, 6}, 15},
    {"SOULA0", "ABCDCB", {6, 6, 6, 6, 6, 6}, 63},
    {"PINVA0", "ABCD", {6, 6, 6, 6}, 15},
    {"PINSA0", "ABCD", {6, 6, 6, 6}, 15},
    {"PMAPA0", "ABCDCB", {6, 6, 6, 6, 6, 6}, 63},
    {"BAR1A0", "AB", {6, 6}, 0},
    {"PVISA0", "AB", {6, 6}, 1},
};

static void make_sprite_animation(Converter *converter, Sprite *sprite, const char *patch) {
    for (size_t index = 0; index < sizeof(thing_animations) / sizeof(thing_animations[0]); ++index) {
        const ThingAnimation *animation = &thing_animations[index];
        if (strcmp(animation->patch, patch)) continue;
        size_t count = strlen(animation->frames);
        if (!count || count > MAX_SPRITE_FRAMES) fail("Invalid sprite animation %s", patch);
        sprite->frame_count = (uint32_t)count;
        for (uint32_t frame = 0; frame < sprite->frame_count; ++frame) {
            char name[9];
            snprintf(name, sizeof(name), "%s", patch);
            name[4] = animation->frames[frame];
            sprite->frames[frame] = (SpriteFrame){.asset = get_asset(converter, name, 2),
                .ticks = animation->ticks[frame], .fullbright = (animation->bright & (1u << frame)) != 0};
        }
        sprite->asset = sprite->frames[0].asset;
        sprite->fullbright = sprite->frames[0].fullbright;
        sprite->frame_ticks = sprite->frames[0].ticks;
        return;
    }
}

static void make_things(Converter *converter) {
    bool spawn_found = false;
    Scene *scene = &converter->scene;
    scene->sprites = allocate(converter->things->size / 10, sizeof(Sprite));
    scene->teleports = allocate(converter->things->size / 10, sizeof(Teleport));
    for (size_t index = 0; index < converter->things->size / 10; ++index) {
        const uint8_t *thing = record(converter->things, index, 10);
        unsigned type = read_u16(thing + 6), flags = read_u16(thing + 8);
        Point point = {read_i16(thing), read_i16(thing + 2)};
        unsigned sector_index = point_sector(converter, point);
        const uint8_t *data = sector(converter, sector_index);
        Position position = {(float)(point.x / 32), (float)read_i16(data) / 32, (float)(-point.y / 32)};
        if (type == 1) {
            scene->spawn = position; scene->spawn.y += 41.0f / 32;
            scene->yaw = (float)read_u16(thing + 4) * 0.01745329252f;
            spawn_found = true;
            continue;
        }
        if (type == 14) {
            scene->teleports[scene->teleport_count++] = (Teleport){.position = position,
                .yaw = (float)read_u16(thing + 4) * 0.01745329252f, .sector = sector_index};
            continue;
        }
        if (type == 2 || type == 3 || type == 4 || type == 11 || type == 87 || type == 89) continue;
        if (!(flags & 2) || (flags & 16)) continue;
        const ThingImage *image = NULL;
        for (size_t image_index = 0; image_index < sizeof(thing_images) / sizeof(thing_images[0]); ++image_index)
            if (thing_images[image_index].type == type) image = &thing_images[image_index];
        if (!image) fail("Unsupported thing type %u", type);
        uint32_t asset = get_asset(converter, image->patch, 2);
        const Asset *patch = &scene->assets[asset];
        if (image->flags & 2) {
            position.y = (float)read_i16(data + 2) / 32 - (float)patch->top / 32;
        } else {
            position.y += asset_floor_offset(patch);
        }
        scene->sprites[scene->sprite_count++] = (Sprite){.asset = asset,
            .sector = sector_index, .position = position, .ceiling = (image->flags & 2) != 0,
            .yaw = (float)read_u16(thing + 4) * 0.01745329252f,
            .fullbright = (image->flags & 1) != 0};
        make_sprite_animation(converter, &scene->sprites[scene->sprite_count - 1], image->patch);
    }
    if (!spawn_found) fail("Map has no player-one start");
}

static void verify_areas(Converter *converter) {
    size_t count = converter->sectors->size / 26;
    double *expected = allocate(count, sizeof(double));
    for (size_t index = 0; index < converter->lines->size / 14; ++index) {
        const uint8_t *line = record(converter->lines, index, 14);
        Point first = vertex(converter, read_u16(line)), second = vertex(converter, read_u16(line + 2));
        double area = (first.x * second.y - second.x * first.y) * 0.5;
        for (unsigned side = 0; side < 2; ++side) {
            if (read_u16(line + 10 + side * 2) != 65535)
                expected[line_sector(converter, line, side)] += side ? area : -area;
        }
    }
    unsigned fallback = 0;
    for (size_t index = 0; index < count; ++index) {
        if (converter->bsp_sectors[index]) { ++fallback; continue; }
        double difference = fabs(fabs(expected[index]) - converter->sector_area[index]);
        if (difference > fmax(0.1, fabs(expected[index]) * 1e-6))
            fail("Sector %zu area mismatch: outline %.6f, triangles %.6f", index,
                 fabs(expected[index]), converter->sector_area[index]);
    }
    printf("Geometry check: all %zu sector areas match their outlines", count - fallback);
    if (fallback) printf("; %u open-outline sectors use original BSP regions", fallback);
    putchar('\n');
    free(expected);
}

static Converter convert_map(const char *path, const char *map) {
    Converter converter = {0};
    converter.wad = wad_read(path);
    size_t marker = converter.wad.count;
    for (size_t index = 0; index < converter.wad.count; ++index)
        if (named(converter.wad.lumps[index].name, map)) { marker = index; break; }
    if (marker == converter.wad.count) fail("Missing map %s", map);
    memcpy(converter.scene.map, map, strlen(map));
    converter.vertices = map_lump(&converter.wad, marker, "VERTEXES", 4);
    converter.lines = map_lump(&converter.wad, marker, "LINEDEFS", 14);
    converter.sides = map_lump(&converter.wad, marker, "SIDEDEFS", 30);
    converter.sectors = map_lump(&converter.wad, marker, "SECTORS", 26);
    converter.segs = map_lump(&converter.wad, marker, "SEGS", 12);
    converter.leaves = map_lump(&converter.wad, marker, "SSECTORS", 4);
    converter.nodes = map_lump(&converter.wad, marker, "NODES", 28);
    converter.things = map_lump(&converter.wad, marker, "THINGS", 10);
    if (!converter.vertices->size || !converter.lines->size || !converter.sectors->size ||
        !converter.leaves->size || !converter.segs->size) fail("Empty map geometry");
    if (converter.nodes->size / 28 > 32767 || converter.leaves->size / 4 > 32768)
        fail("Unsupported extended BSP format");
    for (size_t index = 0; index < converter.sectors->size / 26; ++index) {
        const uint8_t *data = sector(&converter, (unsigned)index);
        if (read_i16(data) > read_i16(data + 2)) fail("Sector floor above ceiling");
    }
    for (size_t index = 0; index < converter.sides->size / 30; ++index)
        sector(&converter, read_u16(record(converter.sides, index, 30) + 28));
    for (size_t index = 0; index < converter.segs->size / 12; ++index) {
        const uint8_t *seg = record(converter.segs, index, 12);
        vertex(&converter, read_u16(seg)); vertex(&converter, read_u16(seg + 2));
        unsigned side = read_u16(seg + 8);
        if (side > 1) fail("Invalid seg side");
        const uint8_t *line = record(converter.lines, read_u16(seg + 6), 14);
        line_sector(&converter, line, side);
    }
    for (size_t index = 0; index < converter.leaves->size / 4; ++index) {
        const uint8_t *leaf = record(converter.leaves, index, 4);
        unsigned count = read_u16(leaf), first = read_u16(leaf + 2);
        if (!count) fail("Empty BSP leaf");
        require_range(converter.segs->size, first * 12, count * 12);
    }
    for (size_t index = 0; index < converter.nodes->size / 28; ++index) {
        const uint8_t *node = record(converter.nodes, index, 28);
        if (!read_i16(node + 4) && !read_i16(node + 6)) fail("Zero length BSP plane");
        for (unsigned side = 0; side < 2; ++side) {
            unsigned child = read_u16(node + 24 + side * 2);
            if (child & 0x8000) record(converter.leaves, child & 0x7fff, 4);
            else if (child >= index) fail("Invalid BSP node order or cycle");
        }
    }
    const Lump *palette = find_lump(&converter.wad, "PLAYPAL"), *colormap = find_lump(&converter.wad, "COLORMAP");
    require_range(palette->size, 0, 768); require_range(colormap->size, 0, 32 * 256);
    for (unsigned level = 0; level < 32; ++level) {
        for (unsigned color = 0; color < 256; ++color) {
            unsigned index = colormap->data[level * 256 + color];
            uint8_t *target = converter.scene.lut + (level * 256 + color) * 4;
            memcpy(target, palette->data + index * 3, 3); target[3] = 255;
        }
    }
    converter.sector_area = allocate(converter.sectors->size / 26, sizeof(double));
    converter.bsp_sectors = allocate(converter.sectors->size / 26, sizeof(bool));
    Scene *scene = &converter.scene;
    scene->sector_count = converter.sectors->size / 26;
    scene->line_count = converter.lines->size / 14;
    scene->sectors = allocate(scene->sector_count, sizeof(SceneSector));
    scene->lines = allocate(scene->line_count, sizeof(CollisionLine));
    for (size_t index = 0; index < scene->sector_count; ++index) {
        const uint8_t *data = sector(&converter, (unsigned)index);
        char floor_name[9] = {0}, ceiling_name[9] = {0};
        memcpy(floor_name, data + 4, 8); memcpy(ceiling_name, data + 12, 8);
        scene->sectors[index] = (SceneSector){.floor = (float)read_i16(data) / 32,
            .ceiling = (float)read_i16(data + 2) / 32, .special = read_u16(data + 22), .tag = read_u16(data + 24),
            .light = read_u16(data + 20), .floor_asset = get_asset(&converter, floor_name, 1),
            .ceiling_asset = named(ceiling_name, "F_SKY1") ? UINT32_MAX : get_asset(&converter, ceiling_name, 1)};
    }
    for (size_t index = 0; index < scene->line_count; ++index) {
        const uint8_t *line = record(converter.lines, index, 14);
        Point start = vertex(&converter, read_u16(line)), end = vertex(&converter, read_u16(line + 2));
        scene->lines[index] = (CollisionLine){
            .start = {(float)(start.x / 32), (float)(-start.y / 32)},
            .end = {(float)(end.x / 32), (float)(-end.y / 32)},
            .front = read_u16(line + 10) == 65535 ? UINT32_MAX : line_sector(&converter, line, 0),
            .back = read_u16(line + 12) == 65535 ? UINT32_MAX : line_sector(&converter, line, 1),
            .flags = read_u16(line + 4), .special = read_u16(line + 6), .tag = read_u16(line + 8)
        };
    }
    make_flats(&converter);
    bool fallback = false;
    for (size_t index = 0; index < scene->sector_count; ++index) fallback = fallback || converter.bsp_sectors[index];
    if (fallback) {
        ClipPolygon bounds = {.count = 4, .points = {{-65536, -65536}, {65536, -65536}, {65536, 65536}, {-65536, 65536}}};
        bsp_flats(&converter, converter.nodes->size ? (unsigned)(converter.nodes->size / 28 - 1) : 0x8000, &bounds, 0);
    }
    verify_areas(&converter);
    make_walls(&converter);
    make_things(&converter);
    for (size_t index = 0; index < converter.wad.count; ++index) {
        if (!strncmp(converter.wad.lumps[index].name, "PLAYA1", 6)) {
            get_asset(&converter, "PLAYA1", 2);
            break;
        }
    }
    return converter;
}

static void self_test(void) {
    TESSreal outer[] = {0, 0, 10, 0, 10, 10, 0, 10};
    TESSreal hole[] = {2, 2, 2, 8, 8, 8, 8, 2};
    TESStesselator *tess = tessNewTess(NULL);
    if (!tess) fail("Cannot create tessellator");
    tessAddContour(tess, 2, outer, (int)(2 * sizeof(TESSreal)), 4);
    tessAddContour(tess, 2, hole, (int)(2 * sizeof(TESSreal)), 4);
    if (!tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 2, NULL)) fail("Tessellation self-test failed");
    double area = 0;
    const TESSreal *points = tessGetVertices(tess);
    const TESSindex *triangles = tessGetElements(tess);
    for (int index = 0; index < tessGetElementCount(tess); ++index) {
        Triangle triangle = {0};
        for (int corner = 0; corner < 3; ++corner) {
            int point = triangles[index * 3 + corner];
            triangle.points[corner] = (Point){points[point * 2], points[point * 2 + 1]};
        }
        area += triangle_area(&triangle);
    }
    tessDeleteTess(tess);
    if (fabs(area - 64) > 1e-8) fail("Hole area self-test failed");
    const uint8_t signed_value[2] = {0, 128};
    if (read_i16(signed_value) != -32768) fail("Endian self-test failed");
    puts("Geometry and endian self-tests passed");
}

static void write_music(const Converter *converter, const char *path) {
    static const char *songs[32] = {"D_RUNNIN", "D_STALKS", "D_COUNTD", "D_BETWEE", "D_DOOM", "D_THE_DA",
        "D_SHAWN", "D_DDTBLU", "D_IN_CIT", "D_DEAD", "D_STLKS2", "D_THEDA2", "D_DOOM2", "D_DDTBL2",
        "D_RUNNI2", "D_DEAD2", "D_STLKS3", "D_ROMERO", "D_SHAWN2", "D_MESSAG", "D_COUNT2", "D_DDTBL3",
        "D_AMPIE", "D_THEDA3", "D_ADRIAN", "D_MESSG2", "D_ROMER2", "D_TENSE", "D_SHAWN3", "D_OPENIN", "D_EVIL", "D_ULTIMA"};
    unsigned map_number = (unsigned)(converter->scene.map[3] - '0') * 10 + (unsigned)(converter->scene.map[4] - '0');
    if (!map_number || map_number > 32) return;
    const char *song = songs[map_number - 1];
    const Lump *music = NULL;
    for (size_t index = 0; index < converter->wad.count; ++index)
        if (named(converter->wad.lumps[index].name, song)) music = &converter->wad.lumps[index];
    if (!music) return;
    uint8_t *midi = NULL;
    size_t size = 0;
    if (!mus_to_midi(music->data, music->size, &midi, &size)) fail("Invalid %.8s MUS music", song);
    char *output = scene_sidecar(path, ".mid");
    write_file(output, midi, size);
    printf("Converted %s to %s (%zu bytes)\n", song, output, size);
    free(output); free(midi);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--self-test")) { self_test(); return 0; }
    if (argc < 3) {
        puts("Usage: wad-convert INPUT.WAD OUTPUT.drmap [MAP01]\n"
             "       [--voxels DIRECTORY --voxeldef FILE (repeat for each definition file)]\n"
             "       wad-convert --self-test");
        return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 1;
    }
    const char *map = "MAP01", *voxel_directory = NULL;
    const char **definitions = allocate((size_t)argc, sizeof(*definitions));
    size_t definition_count = 0;
    for (int index = 3; index < argc; ++index) {
        if (!strcmp(argv[index], "--voxels") && index + 1 < argc) voxel_directory = argv[++index];
        else if (!strcmp(argv[index], "--voxeldef") && index + 1 < argc) definitions[definition_count++] = argv[++index];
        else if (index == 3 && argv[index][0] != '-') map = argv[index];
        else fail("Unknown or incomplete converter option %s", argv[index]);
    }
    if ((voxel_directory != NULL) != (definition_count != 0))
        fail("Voxel conversion requires --voxels DIRECTORY and --voxeldef FILE");
    if (strlen(map) != 5 || strncmp(map, "MAP", 3) || map[3] < '0' || map[3] > '9' ||
        map[4] < '0' || map[4] > '9') fail("Expected a MAPxx name");
    Converter converter = convert_map(argv[1], map);
    if (voxel_directory) {
        convert_voxels(&converter.scene, voxel_directory, definitions, definition_count);
        char *voxel_path = scene_sidecar(argv[2], ".drvox");
        scene_write_voxels(&converter.scene, voxel_path);
        free(voxel_path);
    }
    free(definitions);
    scene_print(&converter.scene);
    scene_write(&converter.scene, argv[2]);
    Scene check = scene_read(argv[2]);
    scene_free(&check);
    printf("Wrote and checked %s\n", argv[2]);
    write_music(&converter, argv[2]);
    scene_free(&converter.scene);
    free(converter.sector_area);
    free(converter.bsp_sectors);
    free(converter.wad.lumps); free(converter.wad.bytes);
    return 0;
}
