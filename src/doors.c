#include "doors.h"

#include <math.h>
#include <stdlib.h>

Doors doors_create(const Scene *scene) {
    return (Doors){.sectors = allocate(scene->sector_count, sizeof(Door)),
                   .switch_ticks = allocate(scene->line_count, sizeof(uint32_t))};
}
void doors_free(Doors *doors) { free(doors->sectors); free(doors->switch_ticks); *doors = (Doors){0}; }

typedef struct { unsigned special, trigger, mode, key; bool manual, repeat, fast; } DoorAction;
enum { TRIGGER_USE, TRIGGER_WALK, TRIGGER_SHOOT };
static const DoorAction actions[] = {
    {1, TRIGGER_USE, DOOR_RAISE, 0, true, true, false}, {31, TRIGGER_USE, DOOR_OPEN_STAY, 0, true, false, false},
    {26, TRIGGER_USE, DOOR_RAISE, 1, true, true, false}, {27, TRIGGER_USE, DOOR_RAISE, 2, true, true, false},
    {28, TRIGGER_USE, DOOR_RAISE, 4, true, true, false}, {32, TRIGGER_USE, DOOR_OPEN_STAY, 1, true, false, false},
    {33, TRIGGER_USE, DOOR_OPEN_STAY, 4, true, false, false}, {34, TRIGGER_USE, DOOR_OPEN_STAY, 2, true, false, false},
    {2, TRIGGER_WALK, DOOR_OPEN_STAY, 0, false, false, false}, {3, TRIGGER_WALK, DOOR_CLOSE, 0, false, false, false},
    {4, TRIGGER_WALK, DOOR_RAISE, 0, false, false, false}, {16, TRIGGER_WALK, DOOR_CLOSE_WAIT_OPEN, 0, false, false, false},
    {29, TRIGGER_USE, DOOR_RAISE, 0, false, false, false}, {42, TRIGGER_USE, DOOR_CLOSE, 0, false, true, false},
    {46, TRIGGER_SHOOT, DOOR_OPEN_STAY, 0, false, true, false}, {61, TRIGGER_USE, DOOR_OPEN_STAY, 0, false, true, false},
    {63, TRIGGER_USE, DOOR_RAISE, 0, false, true, false}, {75, TRIGGER_WALK, DOOR_CLOSE, 0, false, true, false},
    {76, TRIGGER_WALK, DOOR_CLOSE_WAIT_OPEN, 0, false, true, false}, {86, TRIGGER_WALK, DOOR_OPEN_STAY, 0, false, true, false},
    {90, TRIGGER_WALK, DOOR_RAISE, 0, false, true, false}, {103, TRIGGER_USE, DOOR_OPEN_STAY, 0, false, false, false},
    {105, TRIGGER_WALK, DOOR_RAISE, 0, false, true, true}, {106, TRIGGER_WALK, DOOR_OPEN_STAY, 0, false, true, true},
    {107, TRIGGER_WALK, DOOR_CLOSE, 0, false, true, true}, {108, TRIGGER_WALK, DOOR_RAISE, 0, false, false, true},
    {109, TRIGGER_WALK, DOOR_OPEN_STAY, 0, false, false, true}, {110, TRIGGER_WALK, DOOR_CLOSE, 0, false, false, true},
    {111, TRIGGER_USE, DOOR_RAISE, 0, false, false, true}, {112, TRIGGER_USE, DOOR_OPEN_STAY, 0, false, false, true},
    {113, TRIGGER_USE, DOOR_CLOSE, 0, false, false, true}, {114, TRIGGER_USE, DOOR_RAISE, 0, false, true, true},
    {115, TRIGGER_USE, DOOR_OPEN_STAY, 0, false, true, true}, {116, TRIGGER_USE, DOOR_CLOSE, 0, false, true, true},
    {117, TRIGGER_USE, DOOR_RAISE, 0, true, true, true}, {118, TRIGGER_USE, DOOR_OPEN_STAY, 0, true, false, true},
    {99, TRIGGER_USE, DOOR_OPEN_STAY, 1, false, true, true}, {133, TRIGGER_USE, DOOR_OPEN_STAY, 1, false, false, true},
    {134, TRIGGER_USE, DOOR_OPEN_STAY, 4, false, true, true}, {135, TRIGGER_USE, DOOR_OPEN_STAY, 4, false, false, true},
    {136, TRIGGER_USE, DOOR_OPEN_STAY, 2, false, true, true}, {137, TRIGGER_USE, DOOR_OPEN_STAY, 2, false, false, true}
};

static const DoorAction *door_action(unsigned special) {
    for (size_t index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index)
        if (actions[index].special == special) return &actions[index];
    return NULL;
}

static bool start_door(Scene *scene, Doors *doors, uint32_t sector, const DoorAction *action) {
    Door *door = &doors->sectors[sector];
    if (door->state) {
        if (action->manual && action->mode == DOOR_RAISE)
            door->state = door->state == DOOR_CLOSING ? DOOR_OPENING : DOOR_CLOSING;
        return true;
    }
    float top = INFINITY;
    for (size_t index = 0; index < scene->line_count; ++index) {
        const CollisionLine *line = &scene->lines[index];
        uint32_t other = line->front == sector ? line->back : line->back == sector ? line->front : UINT32_MAX;
        if (other != UINT32_MAX && other != sector) top = fminf(top, scene->sectors[other].ceiling);
    }
    top -= 4.0f / 32;
    if (!isfinite(top) || top <= scene->sectors[sector].floor) return false;
    *door = (Door){.top = action->mode == DOOR_CLOSE_WAIT_OPEN ? scene->sectors[sector].ceiling : top,
                  .state = action->mode >= DOOR_CLOSE ? DOOR_CLOSING : DOOR_OPENING,
                  .speed = (action->fast ? 8.0f : 2.0f) / 32, .mode = action->mode};
    return true;
}

static bool activate(Scene *scene, Doors *doors, CollisionLine *line, uint32_t keys) {
    const DoorAction *action = door_action(line->special);
    if (!action || (action->key && !(keys & action->key))) return false;
    bool opened = false;
    if (action->manual) {
        if (line->back != UINT32_MAX) opened = start_door(scene, doors, line->back, action);
    } else {
        if (!line->tag) return false;
        for (uint32_t sector = 0; sector < scene->sector_count; ++sector)
            if (scene->sectors[sector].tag == line->tag) opened = start_door(scene, doors, sector, action) || opened;
    }
    if (opened && !action->repeat) line->special = 0;
    if (opened && !action->manual && action->trigger != TRIGGER_WALK)
        doors->switch_ticks[line - scene->lines] = action->repeat ? 35 : UINT32_MAX;
    return opened;
}

static float ray_line(Position origin, float dx, float dz, const CollisionLine *line) {
    float edge_x = line->end.x - line->start.x, edge_z = line->end.z - line->start.z;
    float denominator = dx * edge_z - dz * edge_x;
    if (fabsf(denominator) < 1e-8f) return -1;
    float offset_x = line->start.x - origin.x, offset_z = line->start.z - origin.z;
    float distance = (offset_x * edge_z - offset_z * edge_x) / denominator;
    float along = (offset_x * dz - offset_z * dx) / denominator;
    return distance >= 0 && along >= 0 && along <= 1 ? distance : -1;
}

static bool trace_door(Scene *scene, Doors *doors, const Player *player, float yaw, bool shoot) {
    float dx = cosf(yaw), dz = -sinf(yaw), nearest = (shoot ? 2048.0f : 64.0f) / 32;
    CollisionLine *target = NULL;
    for (size_t index = 0; index < scene->line_count; ++index) {
        CollisionLine *line = &scene->lines[index];
        float distance = ray_line(player->feet, dx, dz, line);
        if (distance < 0 || distance > nearest) continue;
        bool solid = line->front == UINT32_MAX || line->back == UINT32_MAX || (line->flags & 1);
        if (!solid) {
            float opening = fminf(scene->sectors[line->front].ceiling, scene->sectors[line->back].ceiling) -
                            fmaxf(scene->sectors[line->front].floor, scene->sectors[line->back].floor);
            solid = opening <= 0;
        }
        const DoorAction *action = door_action(line->special);
        if (!solid && (!action || (shoot && action->trigger != TRIGGER_SHOOT))) continue;
        nearest = distance; target = line;
    }
    if (!target) return false;
    float front_side = (target->end.x - target->start.x) * (player->feet.z - target->start.z) -
                       (target->end.z - target->start.z) * (player->feet.x - target->start.x);
    const DoorAction *action = door_action(target->special);
    if (front_side < 0 || !action || action->trigger != (unsigned)(shoot ? TRIGGER_SHOOT : TRIGGER_USE)) return false;
    return activate(scene, doors, target, player->keys);
}

bool doors_use(Scene *scene, Doors *doors, const Player *player, float yaw) {
    return trace_door(scene, doors, player, yaw, false);
}

bool doors_shoot(Scene *scene, Doors *doors, const Player *player, float yaw) {
    return trace_door(scene, doors, player, yaw, true);
}

void doors_cross(Scene *scene, Doors *doors, Position from, Position to) {
    float dx = to.x - from.x, dz = to.z - from.z;
    for (size_t index = 0; index < scene->line_count; ++index) {
        CollisionLine *line = &scene->lines[index];
        const DoorAction *action = door_action(line->special);
        if (!action || action->trigger != TRIGGER_WALK) continue;
        float crossing = ray_line(from, dx, dz, line);
        if (crossing > 0 && crossing <= 1) activate(scene, doors, line, 0);
    }
}

static bool occupied(const Scene *scene, const Player *player, uint32_t sector, float ceiling) {
    if (player->feet.y + PLAYER_HEIGHT <= ceiling) return false;
    if (player->sector == sector) return true;
    for (size_t index = 0; index < scene->line_count; ++index) {
        const CollisionLine *line = &scene->lines[index];
        if (!((line->front == sector && line->back == player->sector) ||
              (line->back == sector && line->front == player->sector))) continue;
        if (line_distance_squared(line, player->feet.x, player->feet.z) <= PLAYER_RADIUS * PLAYER_RADIUS) return true;
    }
    return false;
}

void doors_tick(Scene *scene, Doors *doors, const Player *player) {
    for (size_t index = 0; index < scene->line_count; ++index)
        if (doors->switch_ticks[index] && doors->switch_ticks[index] != UINT32_MAX) --doors->switch_ticks[index];
    for (uint32_t index = 0; index < scene->sector_count; ++index) {
        Door *door = &doors->sectors[index];
        SceneSector *sector = &scene->sectors[index];
        if (door->state == DOOR_OPENING) {
            sector->ceiling = fminf(door->top, sector->ceiling + door->speed);
            if (sector->ceiling >= door->top) {
                door->state = door->mode == DOOR_RAISE ? DOOR_WAIT_OPEN : DOOR_OPEN; door->wait = 150;
            }
        } else if (door->state == DOOR_WAIT_OPEN) {
            if (door->wait && --door->wait == 0) door->state = DOOR_CLOSING;
        } else if (door->state == DOOR_CLOSING) {
            float next = fmaxf(sector->floor, sector->ceiling - door->speed);
            if (occupied(scene, player, index, next)) { if (door->mode != DOOR_CLOSE) door->state = DOOR_OPENING; }
            else {
                sector->ceiling = next;
                if (next <= sector->floor) {
                    door->state = door->mode == DOOR_CLOSE_WAIT_OPEN ? DOOR_WAIT_CLOSED : DOOR_STOPPED; door->wait = 30 * 35;
                }
            }
        } else if (door->state == DOOR_WAIT_CLOSED && door->wait && --door->wait == 0) door->state = DOOR_OPENING;
    }
}
