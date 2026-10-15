#include "voxel_convert.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char model[256];
    float scale, angle, spin;
} VoxelRule;

typedef struct {
    const char *text, *path;
    size_t offset, size;
} Tokens;

typedef struct {
    uint32_t dimensions[3];
    float pivot[3];
    uint8_t *colors, *faces;
} VoxelGrid;

static bool token(Tokens *tokens, char output[256]) {
    const char *text = tokens->text;
    while (tokens->offset < tokens->size) {
        size_t at = tokens->offset;
        if (isspace((unsigned char)text[at])) { ++tokens->offset; continue; }
        if (at + 1 < tokens->size && text[at] == '/' && text[at + 1] == '/') {
            while (tokens->offset < tokens->size && text[tokens->offset] != '\n') ++tokens->offset;
            continue;
        }
        if (at + 1 < tokens->size && text[at] == '/' && text[at + 1] == '*') {
            tokens->offset += 2;
            while (tokens->offset + 1 < tokens->size &&
                   !(text[tokens->offset] == '*' && text[tokens->offset + 1] == '/')) ++tokens->offset;
            if (tokens->offset + 1 >= tokens->size) fail("Unclosed comment in %s", tokens->path);
            tokens->offset += 2;
            continue;
        }
        break;
    }
    if (tokens->offset == tokens->size) return false;
    size_t written = 0;
    char first = text[tokens->offset++];
    if (strchr("={}", first)) {
        output[0] = first; output[1] = 0;
        return true;
    }
    bool quoted = first == '"';
    if (!quoted) output[written++] = first;
    while (tokens->offset < tokens->size) {
        char next = text[tokens->offset];
        if (quoted && next == '"') { ++tokens->offset; output[written] = 0; return true; }
        if (!quoted && (isspace((unsigned char)next) || strchr("={}", next))) break;
        if (written == 255) fail("Voxel definition token too long in %s", tokens->path);
        output[written++] = next;
        ++tokens->offset;
    }
    if (quoted) fail("Unclosed string in %s", tokens->path);
    output[written] = 0;
    return true;
}

static void expect_token(Tokens *tokens, const char *expected) {
    char value[256];
    if (!token(tokens, value) || strcmp(value, expected)) fail("Expected %s in %s", expected, tokens->path);
}

static float number(Tokens *tokens) {
    char value[256], *end;
    if (!token(tokens, value)) fail("Missing voxel option value in %s", tokens->path);
    float result = strtof(value, &end);
    if (!value[0] || *end || !isfinite(result)) fail("Invalid voxel number %s in %s", value, tokens->path);
    return result;
}

static void read_definitions(const Scene *scene, VoxelRule *rules, const char *path) {
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    Tokens tokens = {.text = (const char *)bytes, .size = size, .path = path};
    char value[256];
    bool have_token = token(&tokens, value);
    bool *matches = allocate(scene->asset_count, sizeof(*matches));
    while (have_token) {
        memset(matches, 0, scene->asset_count * sizeof(*matches));
        while (strcmp(value, "=")) {
            size_t length = strlen(value);
            if (length != 4 && length != 5) fail("Invalid voxel sprite key %s in %s", value, path);
            for (size_t character = 0; character < length; ++character)
                value[character] = (char)toupper((unsigned char)value[character]);
            if (length == 5 && (value[4] < 'A' || value[4] > 'Z'))
                fail("Invalid voxel sprite frame %s in %s", value, path);
            for (size_t asset = 0; asset < scene->asset_count; ++asset)
                matches[asset] |= !strncmp(scene->assets[asset].name, value, length);
            if (!token(&tokens, value)) fail("Missing voxel assignment in %s", path);
        }
        VoxelRule rule = {.scale = 1, .angle = 90};
        if (!token(&tokens, rule.model) || !rule.model[0]) fail("Missing voxel model in %s", path);
        if (strchr(rule.model, '/') || strchr(rule.model, '\\') || strstr(rule.model, "..") || strchr(rule.model, ':'))
            fail("Voxel model must be a file name: %s", rule.model);
        have_token = token(&tokens, value);
        if (have_token && !strcmp(value, "{")) {
            while (true) {
                if (!token(&tokens, value)) fail("Unclosed voxel options in %s", path);
                if (!strcmp(value, "}")) break;
                for (char *character = value; *character; ++character)
                    *character = (char)tolower((unsigned char)*character);
                if (!strcmp(value, "useactorpitch") || !strcmp(value, "useactorroll") ||
                    !strcmp(value, "pitchfrommomentum")) continue;
                if (strcmp(value, "scale") && strcmp(value, "angleoffset") && strcmp(value, "placedspin") &&
                    strcmp(value, "droppedspin") && strcmp(value, "spin"))
                    fail("Unsupported voxel option %s in %s", value, path);
                expect_token(&tokens, "=");
                float amount = number(&tokens);
                if (!strcmp(value, "scale")) rule.scale = amount;
                else if (!strcmp(value, "angleoffset")) rule.angle = amount + 90;
                else if (!strcmp(value, "placedspin") || !strcmp(value, "spin")) rule.spin = amount;
            }
            have_token = token(&tokens, value);
        }
        if (rule.scale <= 0 || rule.scale > 64 || fabsf(rule.spin) > 3600 || fabsf(rule.angle) > 360000)
            fail("Voxel scale, angle, or spin is out of range in %s", path);
        for (size_t asset = 0; asset < scene->asset_count; ++asset)
            if (matches[asset]) rules[asset] = rule;
    }
    free(matches);
    free(bytes);
}

static size_t grid_index(const VoxelGrid *grid, uint32_t first, uint32_t second, uint32_t third) {
    return ((size_t)first * grid->dimensions[1] + second) * grid->dimensions[2] + third;
}

static VoxelGrid read_kvx(const uint8_t *bytes, size_t size, uint8_t remap[256], const Scene *scene) {
    require_range(size, 0, 28 + 768);
    size_t mip_end = (size_t)read_u32(bytes) + 4;
    if (mip_end < 28 || mip_end > size - 768) fail("Invalid KVX mip size");
    VoxelGrid grid = {0};
    for (unsigned axis = 0; axis < 3; ++axis) {
        grid.dimensions[axis] = read_u32(bytes + 4 + axis * 4);
        int32_t pivot;
        uint32_t bits = read_u32(bytes + 16 + axis * 4);
        memcpy(&pivot, &bits, sizeof(pivot));
        grid.pivot[axis] = (float)pivot / 256.0f;
        if (!grid.dimensions[axis] || grid.dimensions[axis] > (axis == 2 ? 256u : 1024u))
            fail("Invalid KVX dimensions");
    }
    size_t cells = (size_t)grid.dimensions[0] * grid.dimensions[1] * grid.dimensions[2];
    if (cells > 16777216) fail("KVX grid is too large");
    grid.colors = allocate(cells, 1);
    grid.faces = allocate(cells, 1);
    size_t x_bytes = ((size_t)grid.dimensions[0] + 1) * 4;
    size_t table_bytes = x_bytes + (size_t)grid.dimensions[0] * (grid.dimensions[1] + 1) * 2;
    require_range(mip_end, 28, table_bytes);
    if (read_u32(bytes + 28) != table_bytes || read_u32(bytes + 28 + grid.dimensions[0] * 4) != mip_end - 28)
        fail("Invalid KVX X offsets");
    for (uint32_t first = 0; first < grid.dimensions[0]; ++first) {
        size_t x_start = read_u32(bytes + 28 + first * 4);
        size_t x_end = read_u32(bytes + 28 + (first + 1) * 4);
        if (x_start < table_bytes || x_end < x_start || x_end > mip_end - 28) fail("Invalid KVX X range");
        const uint8_t *columns = bytes + 28 + x_bytes + (size_t)first * (grid.dimensions[1] + 1) * 2;
        if (read_u16(columns) != 0 || read_u16(columns + grid.dimensions[1] * 2) != x_end - x_start)
            fail("Invalid KVX column offsets");
        for (uint32_t second = 0; second < grid.dimensions[1]; ++second) {
            size_t start = read_u16(columns + second * 2), end = read_u16(columns + (second + 1) * 2);
            if (start > end || end > x_end - x_start) fail("Invalid KVX column range");
            start += 28 + x_start; end += 28 + x_start;
            unsigned previous_end = 0;
            while (start < end) {
                require_range(end, start, 3);
                unsigned top = bytes[start], length = bytes[start + 1], faces = bytes[start + 2];
                if (!length || top < previous_end || top + length > grid.dimensions[2] || faces > 63)
                    fail("Invalid KVX slab");
                require_range(end, start + 3, length);
                for (unsigned height = 0; height < length; ++height) {
                    size_t cell = grid_index(&grid, first, second, top + height);
                    grid.colors[cell] = bytes[start + 3 + height];
                    grid.faces[cell] = (uint8_t)((faces & 15) | (height == 0 ? faces & 16 : 0) |
                        (height + 1 == length ? faces & 32 : 0));
                }
                previous_end = top + length;
                start += 3 + length;
            }
        }
    }
    const uint8_t *palette = bytes + size - 768;
    for (unsigned color = 0; color < 256; ++color) {
        int rgb[3];
        for (unsigned channel = 0; channel < 3; ++channel) {
            if (palette[color * 3 + channel] > 63) fail("Invalid KVX palette");
            rgb[channel] = (int)palette[color * 3 + channel] * 4;
        }
        unsigned best_distance = UINT32_MAX;
        remap[color] = 0;
        for (unsigned index = 0; index < 256; ++index) {
            unsigned distance = 0;
            for (unsigned channel = 0; channel < 3; ++channel) {
                int delta = rgb[channel] - scene->lut[index * 4 + channel];
                distance += (unsigned)(delta * delta);
            }
            if (distance < best_distance) { best_distance = distance; remap[color] = (uint8_t)index; }
        }
    }
    return grid;
}

static void mesh_quad(VoxelMesh *mesh, size_t *capacity, const VoxelGrid *grid, const VoxelRule *rule,
                      unsigned axis, unsigned side, uint32_t layer, uint32_t column, uint32_t row,
                      uint32_t width, uint32_t height, uint8_t color) {
    if (mesh->count + 6 > *capacity) {
        size_t next = *capacity ? *capacity * 2 : 768;
        if (mesh->count > 8000000 - 6) fail("Voxel mesh is too large");
        if (next > 8000000) next = 8000000;
        SceneVertex *vertices = realloc(mesh->vertices, next * sizeof(*vertices));
        if (!vertices) fail("Out of memory");
        mesh->vertices = vertices; *capacity = next;
    }
    unsigned across = (axis + 1) % 3, down = (axis + 2) % 3;
    const unsigned columns[4] = {0, 1, 1, 0}, rows[4] = {0, 0, 1, 1};
    const unsigned order[2][6] = {{0, 2, 1, 0, 3, 2}, {0, 1, 2, 0, 2, 3}};
    float scale = rule->scale / 32.0f, angle = rule->angle * 0.01745329252f;
    float cosine = cosf(angle), sine = sinf(angle);
    for (unsigned corner = 0; corner < 6; ++corner) {
        unsigned point = order[side][corner];
        float coordinate[3];
        coordinate[axis] = (float)(layer + side);
        coordinate[across] = (float)(column + columns[point] * width);
        coordinate[down] = (float)(row + rows[point] * height);
        float local_x = (coordinate[0] - grid->pivot[0]) * scale;
        float local_y = (grid->pivot[2] - coordinate[2]) * scale;
        float local_z = (coordinate[1] - grid->pivot[1]) * scale;
        mesh->vertices[mesh->count++] = (SceneVertex){
            cosine * local_x + sine * local_z, local_y, -sine * local_x + cosine * local_z,
            ((float)color + 0.5f) / 256.0f, 0.5f};
    }
}

static VoxelMesh make_mesh(const VoxelGrid *grid, const VoxelRule *rule, const uint8_t remap[256]) {
    VoxelMesh mesh = {.spin = rule->spin};
    size_t capacity = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        unsigned across = (axis + 1) % 3, down = (axis + 2) % 3;
        uint32_t width = grid->dimensions[across], height = grid->dimensions[down];
        uint16_t *mask = allocate((size_t)width * height, sizeof(*mask));
        for (unsigned side = 0; side < 2; ++side) {
            unsigned face_bit = 1u << (axis * 2 + side);
            for (uint32_t layer = 0; layer < grid->dimensions[axis]; ++layer) {
                for (uint32_t row = 0; row < height; ++row) {
                    for (uint32_t column = 0; column < width; ++column) {
                        uint32_t coordinate[3];
                        coordinate[axis] = layer; coordinate[across] = column; coordinate[down] = row;
                        size_t cell = grid_index(grid, coordinate[0], coordinate[1], coordinate[2]);
                        mask[(size_t)row * width + column] = (grid->faces[cell] & face_bit) ?
                            (uint16_t)((unsigned)remap[grid->colors[cell]] + 1) : 0;
                    }
                }
                for (uint32_t row = 0; row < height; ++row) {
                    for (uint32_t column = 0; column < width; ++column) {
                        uint16_t color = mask[(size_t)row * width + column];
                        if (!color) continue;
                        uint32_t run_width = 1, run_height = 1;
                        while (column + run_width < width && mask[(size_t)row * width + column + run_width] == color) ++run_width;
                        bool same = true;
                        while (row + run_height < height && same) {
                            for (uint32_t offset = 0; offset < run_width; ++offset)
                                if (mask[(size_t)(row + run_height) * width + column + offset] != color) { same = false; break; }
                            if (same) ++run_height;
                        }
                        mesh_quad(&mesh, &capacity, grid, rule, axis, side, layer, column, row,
                            run_width, run_height, (uint8_t)(color - 1));
                        for (uint32_t offset = 0; offset < run_height; ++offset)
                            memset(mask + (size_t)(row + offset) * width + column, 0, run_width * sizeof(*mask));
                    }
                }
            }
        }
        free(mask);
    }
    return mesh;
}

void convert_voxels(Scene *scene, const char *directory, const char *const *definitions, size_t count) {
    VoxelRule *rules = allocate(scene->asset_count, sizeof(*rules));
    for (size_t index = 0; index < count; ++index) read_definitions(scene, rules, definitions[index]);
    for (size_t index = 0; index < scene->asset_count; ++index) {
        const VoxelRule *rule = &rules[index];
        if (!rule->model[0]) continue;
        bool sprite_asset = false;
        for (size_t sprite = 0; sprite < scene->sprite_count; ++sprite) {
            const Sprite *thing = &scene->sprites[sprite];
            sprite_asset |= thing->asset == index;
            for (uint32_t frame = 0; frame < thing->frame_count; ++frame)
                sprite_asset |= thing->frames[frame].asset == index;
        }
        if (!sprite_asset) continue;
        size_t path_size = strlen(directory) + strlen(rule->model) + 7;
        char *path = allocate(path_size, 1);
        snprintf(path, path_size, "%s/%s%s", directory, rule->model, strchr(rule->model, '.') ? "" : ".kvx");
        FILE *file = fopen(path, "rb");
        if (!file) {
            fprintf(stderr, "Voxel %s is missing; keeping sprite %s\n", path, scene->assets[index].name);
            free(path);
            continue;
        }
        fclose(file);
        size_t size;
        uint8_t *bytes = read_file(path, &size), remap[256];
        VoxelGrid grid = read_kvx(bytes, size, remap, scene);
        scene->assets[index].voxel = make_mesh(&grid, rule, remap);
        free(grid.colors); free(grid.faces); free(bytes); free(path);
    }
    free(rules);
}
