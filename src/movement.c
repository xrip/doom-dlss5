#include "movement.h"

#include <math.h>
#include <string.h>

static constexpr float CONTACT_GAP = 0.0001f;
static constexpr float GRAVITY = 38.28125f;

static double side_of(MapPoint start, MapPoint end, float x, float z) {
    return ((double)end.x - start.x) * ((double)z - start.z) -
           ((double)end.z - start.z) * ((double)x - start.x);
}

static bool contains(const CollisionTriangle *region, float x, float z) {
    double first = side_of(region->points[0], region->points[1], x, z);
    double second = side_of(region->points[1], region->points[2], x, z);
    double third = side_of(region->points[2], region->points[0], x, z);
    return (first >= -1e-8 && second >= -1e-8 && third >= -1e-8) ||
           (first <= 1e-8 && second <= 1e-8 && third <= 1e-8);
}

uint32_t scene_sector_at(const Scene *scene, float x, float z, uint32_t hint) {
    uint32_t found = UINT32_MAX;
    for (size_t index = 0; index < scene->region_count; ++index) {
        const CollisionTriangle *region = &scene->regions[index];
        if (!contains(region, x, z)) continue;
        if (region->sector == hint) return hint;
        found = region->sector;
    }
    return found;
}

Player player_spawn(const Scene *scene) {
    Player player = {.feet = scene->spawn, .view_height = PLAYER_EYE};
    player.sector = scene_sector_at(scene, player.feet.x, player.feet.z, UINT32_MAX);
    if (player.sector == UINT32_MAX) fail("Player start outside collision regions");
    const SceneSector *sector = &scene->sectors[player.sector];
    if (sector->ceiling - sector->floor < PLAYER_HEIGHT) fail("Player start has no headroom");
    player.feet.y = sector->floor;
    return player;
}

static bool blocks(const Scene *scene, const CollisionLine *line, float feet) {
    if (line->front == UINT32_MAX || line->back == UINT32_MAX || (line->flags & 1)) return true;
    const SceneSector *front = &scene->sectors[line->front], *back = &scene->sectors[line->back];
    float floor = fmaxf(front->floor, back->floor);
    float ceiling = fminf(front->ceiling, back->ceiling);
    return ceiling - floor < PLAYER_HEIGHT - CONTACT_GAP ||
           ceiling - feet < PLAYER_HEIGHT - CONTACT_GAP || floor - feet > PLAYER_STEP + CONTACT_GAP;
}

typedef struct { float time, nx, nz; bool hit; } Contact;

static void keep_contact(Contact *contact, float time, float nx, float nz, float dx, float dz) {
    if (time < 0 || time > contact->time || nx * dx + nz * dz >= -1e-8f) return;
    *contact = (Contact){time, nx, nz, true};
}

static void sweep_endpoint(Contact *contact, MapPoint endpoint, float x, float z, float dx, float dz) {
    double offset_x = (double)x - endpoint.x, offset_z = (double)z - endpoint.z;
    double speed_squared = (double)dx * dx + (double)dz * dz;
    double along = offset_x * dx + offset_z * dz;
    double outside = offset_x * offset_x + offset_z * offset_z - (double)PLAYER_RADIUS * PLAYER_RADIUS;
    if (speed_squared < 1e-16 || along >= 0) return;
    double discriminant = along * along - speed_squared * outside;
    if (discriminant < 0) return;
    float time = outside <= 0 ? 0 : (float)((-along - sqrt(discriminant)) / speed_squared);
    float nx = (float)offset_x + dx * time, nz = (float)offset_z + dz * time;
    float length = hypotf(nx, nz);
    if (length > 1e-8f) keep_contact(contact, time, nx / length, nz / length, dx, dz);
}

static void sweep_line(Contact *contact, const CollisionLine *line, float x, float z, float dx, float dz) {
    float tangent_x = line->end.x - line->start.x, tangent_z = line->end.z - line->start.z;
    float length = hypotf(tangent_x, tangent_z);
    tangent_x /= length; tangent_z /= length;
    float nx = -tangent_z, nz = tangent_x;
    float distance = (x - line->start.x) * nx + (z - line->start.z) * nz;
    float sign = distance >= 0 ? 1 : -1;
    nx *= sign; nz *= sign;
    float approach = dx * nx + dz * nz;
    if (approach < -1e-8f) {
        float time = fmaxf(0, (PLAYER_RADIUS - fabsf(distance)) / approach);
        float along = (x + dx * time - line->start.x) * tangent_x + (z + dz * time - line->start.z) * tangent_z;
        if (along >= 0 && along <= length) keep_contact(contact, time, nx, nz, dx, dz);
    }
    sweep_endpoint(contact, line->start, x, z, dx, dz);
    sweep_endpoint(contact, line->end, x, z, dx, dz);
}

static void slide(const Scene *scene, Player *player, float dx, float dz) {
    for (unsigned attempt = 0; attempt < 6 && hypotf(dx, dz) > 1e-7f; ++attempt) {
        Contact contact = {.time = 1};
        for (size_t index = 0; index < scene->line_count; ++index) {
            const CollisionLine *line = &scene->lines[index];
            if (blocks(scene, line, player->feet.y))
                sweep_line(&contact, line, player->feet.x, player->feet.z, dx, dz);
        }
        float fraction = contact.hit ? fmaxf(0, contact.time - CONTACT_GAP / hypotf(dx, dz)) : 1;
        player->feet.x += dx * fraction; player->feet.z += dz * fraction;
        if (!contact.hit) break;
        dx *= 1 - fraction; dz *= 1 - fraction;
        float into = dx * contact.nx + dz * contact.nz;
        dx -= contact.nx * into; dz -= contact.nz * into;
    }
}

static void clearance(const Scene *scene, const Player *player, float *floor, float *ceiling) {
    *floor = scene->sectors[player->sector].floor;
    *ceiling = scene->sectors[player->sector].ceiling;
    for (size_t index = 0; index < scene->line_count; ++index) {
        const CollisionLine *line = &scene->lines[index];
        if (line->front != player->sector && line->back != player->sector) continue;
        if (blocks(scene, line, player->feet.y) || line_distance_squared(line, player->feet.x, player->feet.z) > PLAYER_RADIUS * PLAYER_RADIUS)
            continue;
        *floor = fmaxf(*floor, fmaxf(scene->sectors[line->front].floor, scene->sectors[line->back].floor));
        *ceiling = fminf(*ceiling, fminf(scene->sectors[line->front].ceiling, scene->sectors[line->back].ceiling));
    }
}

static void settle(const Scene *scene, Player *player, float seconds) {
    float floor, ceiling;
    clearance(scene, player, &floor, &ceiling);
    if (player->feet.y > floor) {
        player->feet.y += player->vertical_speed * seconds - GRAVITY * seconds * seconds * 0.5f;
        player->vertical_speed -= GRAVITY * seconds;
    }
    if (player->feet.y <= floor) { player->feet.y = floor; player->vertical_speed = 0; }
    if (player->feet.y + PLAYER_HEIGHT > ceiling) {
        player->feet.y = fmaxf(floor, ceiling - PLAYER_HEIGHT);
        player->vertical_speed = fminf(player->vertical_speed, 0);
    }
}

void player_move(const Scene *scene, Player *player, float dx, float dz, float seconds) {
    unsigned steps = (unsigned)fmaxf(1, ceilf(hypotf(dx, dz) / (PLAYER_RADIUS * 0.5f)));
    float step_x = dx / (float)steps, step_z = dz / (float)steps, step_time = seconds / (float)steps;
    for (unsigned step = 0; step < steps; ++step) {
        Player previous = *player;
        slide(scene, player, step_x, step_z);
        uint32_t sector = scene_sector_at(scene, player->feet.x, player->feet.z, player->sector);
        if (sector == UINT32_MAX) *player = previous;
        else {
            player->sector = sector;
            float floor, ceiling;
            clearance(scene, player, &floor, &ceiling);
            if (ceiling - floor < PLAYER_HEIGHT - CONTACT_GAP ||
                ceiling - player->feet.y < PLAYER_HEIGHT - CONTACT_GAP ||
                floor - player->feet.y > PLAYER_STEP + CONTACT_GAP) *player = previous;
        }
        settle(scene, player, step_time);
    }
}

void player_tick(const Scene *scene, Player *player, int forward, int sideways, float yaw, bool running) {
    float floor, ceiling;
    clearance(scene, player, &floor, &ceiling);
    bool grounded = player->feet.y <= floor + CONTACT_GAP;
    if (grounded) {
        float forward_accel = (float)forward * (running ? 50 : 25) / 1024;
        float side_accel = (float)sideways * (running ? 40 : 24) / 1024;
        player->velocity_x += cosf(yaw) * forward_accel + sinf(yaw) * side_accel;
        player->velocity_z += -sinf(yaw) * forward_accel + cosf(yaw) * side_accel;
    }
    player->velocity_x = fmaxf(-30.0f / 32, fminf(30.0f / 32, player->velocity_x));
    player->velocity_z = fmaxf(-30.0f / 32, fminf(30.0f / 32, player->velocity_z));
    Position previous = player->feet;
    player_move(scene, player, player->velocity_x, player->velocity_z, 1.0f / 35);
    player->velocity_x = player->feet.x - previous.x;
    player->velocity_z = player->feet.z - previous.z;
    clearance(scene, player, &floor, &ceiling);
    if (player->feet.y <= floor + CONTACT_GAP) {
        player->velocity_x *= 29.0f / 32; player->velocity_z *= 29.0f / 32;
        if (!forward && !sideways && fabsf(player->velocity_x) < 0.0625f / 32 &&
            fabsf(player->velocity_z) < 0.0625f / 32) player->velocity_x = player->velocity_z = 0;
    }
    float rise = player->feet.y - previous.y;
    if (rise > 0) { player->view_height -= rise; player->view_speed = 0.25f / 32; }
    player->view_height += player->view_speed;
    if (player->view_height > PLAYER_EYE) { player->view_height = PLAYER_EYE; player->view_speed = 0; }
    if (player->view_height < PLAYER_EYE * 0.5f) player->view_height = PLAYER_EYE * 0.5f;
    if (player->view_speed != 0) player->view_speed += 0.25f / 32;
    ++player->ticks;
}

float player_view_y(const Scene *scene, const Player *player) {
    float speed_squared = player->velocity_x * player->velocity_x + player->velocity_z * player->velocity_z;
    float bob = fminf(speed_squared * 8, 16.0f / 32) * 0.5f;
    float phase = (float)(player->ticks % 20) * 0.31415926536f;
    return fminf(player->feet.y + player->view_height + sinf(phase) * bob,
                 scene->sectors[player->sector].ceiling - 4.0f / 32);
}

void player_pickup_keys(Scene *scene, Player *player) {
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        Sprite *sprite = &scene->sprites[index];
        const char *name = scene->assets[sprite->asset].name;
        uint32_t key = !strncmp(name, "BKEY", 4) || !strncmp(name, "BSKU", 4) ? 1u :
                       !strncmp(name, "YKEY", 4) || !strncmp(name, "YSKU", 4) ? 2u :
                       !strncmp(name, "RKEY", 4) || !strncmp(name, "RSKU", 4) ? 4u : 0;
        if (!key || sprite->hidden || fabsf(sprite->position.y - player->feet.y) > PLAYER_HEIGHT ||
            hypotf(sprite->position.x - player->feet.x, sprite->position.z - player->feet.z) > 36.0f / 32) continue;
        player->keys |= key; sprite->hidden = true;
    }
}
