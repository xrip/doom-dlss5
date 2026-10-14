#include "specials.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void init_effects(const Scene *scene, Specials *specials);

Specials specials_create(const Scene *scene) {
    SectorMovers movers = sector_movers_create(scene);
    Specials specials = {.doors = allocate(scene->sector_count, sizeof(Door)), .movers = movers,
        .switch_ticks = allocate(scene->line_count, sizeof(uint32_t)),
        .lights = allocate(scene->sector_count, sizeof(SectorLight)), .random = 1};
    init_effects(scene, &specials);
    return specials;
}
void specials_free(Specials *specials) {
    free(specials->doors); sector_movers_free(&specials->movers);
    free(specials->switch_ticks); free(specials->lights); *specials = (Specials){0};
}

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

enum { LIGHT_MINIMUM, LIGHT_MAXIMUM, LIGHT_DARK, LIGHT_BRIGHT, LIGHT_STROBE };
typedef struct { unsigned special, trigger, mode; bool repeat; } LightAction;
static const LightAction light_actions[] = {
    {12, TRIGGER_WALK, LIGHT_MAXIMUM, false}, {13, TRIGGER_WALK, LIGHT_BRIGHT, false},
    {17, TRIGGER_WALK, LIGHT_STROBE, false}, {35, TRIGGER_WALK, LIGHT_DARK, false},
    {79, TRIGGER_WALK, LIGHT_DARK, true}, {80, TRIGGER_WALK, LIGHT_MAXIMUM, true},
    {81, TRIGGER_WALK, LIGHT_BRIGHT, true}, {104, TRIGGER_WALK, LIGHT_MINIMUM, false},
    {138, TRIGGER_USE, LIGHT_BRIGHT, true}, {139, TRIGGER_USE, LIGHT_DARK, true}
};

static const LightAction *light_action(unsigned special) {
    for (size_t index = 0; index < sizeof(light_actions) / sizeof(light_actions[0]); ++index)
        if (light_actions[index].special == special) return &light_actions[index];
    return NULL;
}

static unsigned action_trigger(unsigned special) {
    const DoorAction *door = door_action(special);
    const SectorAction *floor = sector_action(special);
    const LightAction *light = light_action(special);
    if (special == 11 || special == 51) return TRIGGER_USE;
    if (special == 39 || special == 97 || special == 125 || special == 126 || special == 52 || special == 124)
        return TRIGGER_WALK;
    return door ? door->trigger : floor ? floor->trigger : light ? light->trigger : UINT32_MAX;
}

bool specials_supported(unsigned special) { return special == 48 || action_trigger(special) != UINT32_MAX; }

static unsigned random_value(Specials *specials) {
    specials->random ^= specials->random << 13;
    specials->random ^= specials->random >> 17;
    specials->random ^= specials->random << 5;
    return specials->random & 255;
}

static unsigned neighbor_light(const Scene *scene, uint32_t sector, bool highest) {
    unsigned result = highest ? 0 : scene->sectors[sector].light;
    for (size_t index = 0; index < scene->line_count; ++index) {
        uint32_t other = line_neighbor(&scene->lines[index], sector);
        if (other == UINT32_MAX) continue;
        unsigned light = scene->sectors[other].light;
        if (highest ? light > result : light < result) result = light;
    }
    return result;
}

static void init_effects(const Scene *scene, Specials *specials) {
    for (uint32_t sector = 0; sector < scene->sector_count; ++sector) {
        const SceneSector *data = &scene->sectors[sector];
        if (data->special == 9) ++specials->total_secrets;
        SectorLight *light = &specials->lights[sector];
        if (data->special == 1 || data->special == 2 || data->special == 3 || data->special == 4 ||
            data->special == 8 || data->special == 12 || data->special == 13 || data->special == 17) {
            *light = (SectorLight){.kind = data->special, .minimum = neighbor_light(scene, sector, false),
                .maximum = data->light, .wait = data->special == 12 || data->special == 13 ? 1 :
                    (random_value(specials) & 7) + 1};
            if (light->minimum == light->maximum && data->special != 8) light->minimum = 0;
            if (data->special == 17) { light->minimum += 16; light->wait = 4; }
            if (light->minimum > light->maximum) light->minimum = light->maximum;
        }
        if (data->special == 10 || data->special == 14) {
            float top = INFINITY;
            for (size_t index = 0; index < scene->line_count; ++index) {
                uint32_t other = line_neighbor(&scene->lines[index], sector);
                if (other != UINT32_MAX) top = fminf(top, scene->sectors[other].ceiling);
            }
            specials->doors[sector] = (Door){.top = isfinite(top) ? top - 4.0f / 32 : data->ceiling,
                .state = data->special == 10 ? DOOR_WAIT_OPEN : DOOR_WAIT_CLOSED,
                .wait = data->special == 10 ? 30 * 35 : 300 * 35, .speed = 2.0f / 32,
                .mode = data->special == 10 ? DOOR_CLOSE : DOOR_OPEN_STAY};
        }
    }
}

static void tick_light(Scene *scene, Specials *specials, uint32_t sector) {
    SectorLight *light = &specials->lights[sector];
    SceneSector *data = &scene->sectors[sector];
    if (!light->kind) return;
    if (light->kind == 8) {
        if (light->rising) {
            data->light = data->light + 8 >= light->maximum ? light->maximum : data->light + 8;
            if (data->light == light->maximum) light->rising = false;
        } else {
            data->light = data->light <= light->minimum + 8 ? light->minimum : data->light - 8;
            if (data->light == light->minimum) light->rising = true;
        }
        return;
    }
    if (light->wait && --light->wait) return;
    if (light->kind == 17) {
        unsigned amount = (random_value(specials) & 3) * 16;
        data->light = light->maximum - light->minimum < amount ? light->minimum : light->maximum - amount;
        light->wait = 4;
    } else {
        bool bright = data->light == light->maximum;
        data->light = bright ? light->minimum : light->maximum;
        if (light->kind == 1) light->wait = (random_value(specials) & (bright ? 7u : 63u)) + 1;
        else light->wait = bright ? (light->kind == 2 || light->kind == 4 || light->kind == 12 ? 15u : 35u) : 5u;
    }
}

static void change_light(Scene *scene, Specials *specials, uint32_t sector, const LightAction *action) {
    SectorLight *light = &specials->lights[sector];
    if (action->mode == LIGHT_STROBE) {
        *light = (SectorLight){.kind = 3, .minimum = neighbor_light(scene, sector, false),
            .maximum = scene->sectors[sector].light, .wait = (random_value(specials) & 7) + 1};
        if (light->minimum == light->maximum) light->minimum = 0;
    } else {
        unsigned value = action->mode == LIGHT_BRIGHT ? 255 : action->mode == LIGHT_DARK ? 35 :
                         neighbor_light(scene, sector, action->mode == LIGHT_MAXIMUM);
        scene->sectors[sector].light = value;
    }
}

static bool start_door(Scene *scene, Specials *specials, uint32_t sector, const DoorAction *action) {
    if (sector_movers_busy(&specials->movers, sector)) return false;
    Door *door = &specials->doors[sector];
    if (door->state != DOOR_STOPPED && door->state != DOOR_OPEN) {
        if (action->manual && action->mode == DOOR_RAISE) {
            door->state = door->state == DOOR_CLOSING ? DOOR_OPENING : DOOR_CLOSING;
            return true;
        }
        return false;
    }
    float top = INFINITY;
    for (size_t index = 0; index < scene->line_count; ++index) {
        const CollisionLine *line = &scene->lines[index];
        uint32_t other = line_neighbor(line, sector);
        if (other != UINT32_MAX) top = fminf(top, scene->sectors[other].ceiling);
    }
    top -= 4.0f / 32;
    if (!isfinite(top) || top <= scene->sectors[sector].floor) return false;
    *door = (Door){.top = action->mode == DOOR_CLOSE_WAIT_OPEN ? scene->sectors[sector].ceiling : top,
                  .state = action->mode >= DOOR_CLOSE ? DOOR_CLOSING : DOOR_OPENING,
                  .speed = (action->fast ? 8.0f : 2.0f) / 32, .mode = action->mode};
    return true;
}

static bool activate(Scene *scene, Specials *specials, CollisionLine *line, uint32_t keys) {
    if (specials->exited) return false;
    if (line->special == 11 || line->special == 51 || line->special == 52 || line->special == 124) {
        specials->exited = true; specials->secret_exit = line->special == 51 || line->special == 124;
        if (line->special == 11 || line->special == 51) specials->switch_ticks[line - scene->lines] = UINT32_MAX;
        line->special = 0;
        return true;
    }
    const DoorAction *door = door_action(line->special);
    const SectorAction *floor = sector_action(line->special);
    const LightAction *light = light_action(line->special);
    if ((!door && !floor && !light) || (door && door->key && !(keys & door->key))) return false;
    bool manual = door && door->manual;
    bool repeat = door ? door->repeat : floor ? floor->repeat : light->repeat;
    unsigned trigger = action_trigger(line->special);
    if (!manual && !line->tag) return false;
    bool started = false;
    for (uint32_t sector = 0; sector < scene->sector_count; ++sector) {
        if (manual ? sector != line->back : scene->sectors[sector].tag != line->tag) continue;
        bool result;
        if (door) result = start_door(scene, specials, sector, door);
        else if (light) { change_light(scene, specials, sector, light); result = true; }
        else {
            unsigned state = specials->doors[sector].state;
            result = (state == DOOR_STOPPED || state == DOOR_OPEN) &&
                sector_movers_start(scene, &specials->movers, sector, floor, line);
        }
        started = result || started;
    }
    if (started && !repeat) line->special = 0;
    if (started && !manual && trigger != TRIGGER_WALK)
        specials->switch_ticks[line - scene->lines] = repeat ? 35 : UINT32_MAX;
    return started;
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

static bool trace_special(Scene *scene, Specials *specials, const Player *player, float yaw, bool shoot) {
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
        unsigned trigger = action_trigger(line->special);
        if (!solid && (trigger == UINT32_MAX || (shoot && trigger != TRIGGER_SHOOT))) continue;
        nearest = distance; target = line;
    }
    if (!target) return false;
    float front_side = (target->end.x - target->start.x) * (player->feet.z - target->start.z) -
                       (target->end.z - target->start.z) * (player->feet.x - target->start.x);
    unsigned trigger = action_trigger(target->special);
    if (front_side < 0 || trigger != (unsigned)(shoot ? TRIGGER_SHOOT : TRIGGER_USE)) return false;
    return activate(scene, specials, target, player->keys);
}

bool specials_use(Scene *scene, Specials *specials, const Player *player, float yaw) {
    return trace_special(scene, specials, player, yaw, false);
}

bool specials_shoot(Scene *scene, Specials *specials, const Player *player, float yaw) {
    return trace_special(scene, specials, player, yaw, true);
}

static float line_side(const CollisionLine *line, Position point) {
    return (line->end.x - line->start.x) * (point.z - line->start.z) -
           (line->end.z - line->start.z) * (point.x - line->start.x);
}

static bool teleport_player(Scene *scene, Specials *specials, CollisionLine *line, Player *player) {
    if (!player || !line->tag || player->teleport_wait) return false;
    for (uint32_t sector = 0; sector < scene->sector_count; ++sector) {
        if (scene->sectors[sector].tag != line->tag) continue;
        for (size_t index = 0; index < scene->teleport_count; ++index) {
            const Teleport *destination = &scene->teleports[index];
            if (destination->sector != sector || scene->sectors[sector].ceiling - scene->sectors[sector].floor < PLAYER_HEIGHT) continue;
            player->feet = destination->position; player->feet.y = scene->sectors[sector].floor; player->sector = sector;
            player->velocity_x = player->velocity_z = player->vertical_speed = player->view_speed = 0;
            player->view_height = PLAYER_EYE; player->teleport_wait = 18; specials->teleport_yaw = destination->yaw;
            if (line->special == 39) line->special = 0;
            return true;
        }
    }
    return false;
}

bool specials_cross(Scene *scene, Specials *specials, Position from, Position to, Player *player) {
    float dx = to.x - from.x, dz = to.z - from.z;
    for (size_t index = 0; index < scene->line_count; ++index) {
        CollisionLine *line = &scene->lines[index];
        if (action_trigger(line->special) != TRIGGER_WALK) continue;
        bool from_back = line_side(line, from) < 0, to_back = line_side(line, to) < 0;
        if (from_back == to_back) continue;
        float crossing = ray_line(from, dx, dz, line);
        if (crossing < 0 || crossing > 1) continue;
        if (line->special == 39 || line->special == 97) {
            if (!from_back && to_back && teleport_player(scene, specials, line, player)) return true;
        } else if (line->special != 125 && line->special != 126) activate(scene, specials, line, player ? player->keys : 0);
    }
    return false;
}

void specials_tick(Scene *scene, Specials *specials, Player *player) {
    ++specials->ticks;
    if (specials->secret_message) --specials->secret_message;
    for (size_t index = 0; index < scene->wall_count; ++index)
        if (scene->lines[scene->walls[index].line].special == 48 && !scene->walls[index].side)
            scene->walls[index].offset_x += 1;
    for (size_t index = 0; index < scene->line_count; ++index)
        if (specials->switch_ticks[index] && specials->switch_ticks[index] != UINT32_MAX) --specials->switch_ticks[index];
    for (uint32_t index = 0; index < scene->sector_count; ++index) {
        Door *door = &specials->doors[index];
        SceneSector *sector = &scene->sectors[index];
        float previous_ceiling = sector->ceiling;
        if (door->state == DOOR_OPENING) {
            sector->ceiling = fminf(door->top, sector->ceiling + door->speed);
            if (sector->ceiling >= door->top) {
                door->state = door->mode == DOOR_RAISE ? DOOR_WAIT_OPEN : DOOR_OPEN; door->wait = 150;
            }
        } else if (door->state == DOOR_WAIT_OPEN) {
            if (door->wait && --door->wait == 0) door->state = DOOR_CLOSING;
        } else if (door->state == DOOR_CLOSING) {
            float next = fmaxf(sector->floor, sector->ceiling - door->speed);
            if (player && player->feet.y + PLAYER_HEIGHT > next && player_touches_sector(scene, player, index)) {
                if (door->mode != DOOR_CLOSE) door->state = DOOR_OPENING;
            }
            else {
                sector->ceiling = next;
                if (next <= sector->floor) {
                    door->state = door->mode == DOOR_CLOSE_WAIT_OPEN ? DOOR_WAIT_CLOSED : DOOR_STOPPED; door->wait = 30 * 35;
                }
            }
        } else if (door->state == DOOR_WAIT_CLOSED && door->wait && --door->wait == 0) door->state = DOOR_OPENING;
        if (sector->ceiling != previous_ceiling)
            for (size_t sprite = 0; sprite < scene->sprite_count; ++sprite)
                if (scene->sprites[sprite].ceiling && scene->sprites[sprite].sector == index)
                    scene->sprites[sprite].position.y += sector->ceiling - previous_ceiling;
        sector_movers_tick(scene, &specials->movers, index, player);
        tick_light(scene, specials, index);
    }
}

void specials_player(Scene *scene, Specials *specials, Player *player) {
    if (player->teleport_wait) --player->teleport_wait;
    SceneSector *sector = &scene->sectors[player->sector];
    if (fabsf(player->feet.y - sector->floor) > 0.0001f) return;
    if (sector->special == 9) {
        sector->special = 0; ++specials->secrets; specials->secret_message = 3 * 35;
    }
    if ((specials->ticks & 31) || !player->health) return;
    unsigned damage = sector->special == 5 ? 10 : sector->special == 7 ? 5 :
        sector->special == 4 || sector->special == 16 || sector->special == 11 ? 20 : 0;
    player->health = player->health > damage ? player->health - damage : 0;
    if (sector->special == 11 && player->health < 11) specials->exited = true;
}

char *specials_next_map(const Scene *scene, const Specials *specials, const char *path) {
    if (strncmp(scene->map, "MAP", 3) || scene->map[3] < '0' || scene->map[3] > '9' ||
        scene->map[4] < '0' || scene->map[4] > '9' || scene->map[5]) return NULL;
    unsigned map = (unsigned)(scene->map[3] - '0') * 10 + (unsigned)(scene->map[4] - '0');
    if (!map || map > 32 || (map == 30 && !specials->secret_exit)) return NULL;
    unsigned next = specials->secret_exit ? (map == 15 ? 31 : map == 31 ? 32 : map) : map >= 31 ? 16 : map + 1;
    const char *name = path;
    for (const char *cursor = path; *cursor; ++cursor) if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    size_t prefix = (size_t)(name - path);
    char *result = allocate(prefix + 12, 1);
    memcpy(result, path, prefix); snprintf(result + prefix, 12, "MAP%02u.drmap", next);
    return result;
}
