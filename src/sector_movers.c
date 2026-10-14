#include "sector_movers.h"

#include <math.h>
#include <stdlib.h>

static const SectorAction actions[] = {
    {.special = 102, .trigger = SECTOR_USE, .mode = SECTOR_LOWER_HIGHEST, .speed = 1},
    {.special = 45, .trigger = SECTOR_USE, .mode = SECTOR_LOWER_HIGHEST, .repeat = true, .speed = 1},
    {.special = 19, .trigger = SECTOR_WALK, .mode = SECTOR_LOWER_HIGHEST, .speed = 1},
    {.special = 83, .trigger = SECTOR_WALK, .mode = SECTOR_LOWER_HIGHEST, .repeat = true, .speed = 1},
    {.special = 23, .trigger = SECTOR_USE, .mode = SECTOR_LOWER_LOWEST, .speed = 1},
    {.special = 60, .trigger = SECTOR_USE, .mode = SECTOR_LOWER_LOWEST, .repeat = true, .speed = 1},
    {.special = 38, .trigger = SECTOR_WALK, .mode = SECTOR_LOWER_LOWEST, .speed = 1},
    {.special = 82, .trigger = SECTOR_WALK, .mode = SECTOR_LOWER_LOWEST, .repeat = true, .speed = 1},
    {.special = 71, .trigger = SECTOR_USE, .mode = SECTOR_LOWER_TURBO, .speed = 4},
    {.special = 70, .trigger = SECTOR_USE, .mode = SECTOR_LOWER_TURBO, .repeat = true, .speed = 4},
    {.special = 36, .trigger = SECTOR_WALK, .mode = SECTOR_LOWER_TURBO, .speed = 4},
    {.special = 98, .trigger = SECTOR_WALK, .mode = SECTOR_LOWER_TURBO, .repeat = true, .speed = 4},
    {.special = 21, .trigger = SECTOR_USE, .mode = SECTOR_LIFT_RETURN, .speed = 4},
    {.special = 62, .trigger = SECTOR_USE, .mode = SECTOR_LIFT_RETURN, .repeat = true, .speed = 4},
    {.special = 10, .trigger = SECTOR_WALK, .mode = SECTOR_LIFT_RETURN, .speed = 4},
    {.special = 88, .trigger = SECTOR_WALK, .mode = SECTOR_LIFT_RETURN, .repeat = true, .speed = 4},
    {.special = 122, .trigger = SECTOR_USE, .mode = SECTOR_LIFT_RETURN, .speed = 8},
    {.special = 123, .trigger = SECTOR_USE, .mode = SECTOR_LIFT_RETURN, .repeat = true, .speed = 8},
    {.special = 121, .trigger = SECTOR_WALK, .mode = SECTOR_LIFT_RETURN, .speed = 8},
    {.special = 120, .trigger = SECTOR_WALK, .mode = SECTOR_LIFT_RETURN, .repeat = true, .speed = 8},
    {.special = 5, .trigger = SECTOR_WALK, .mode = SECTOR_RAISE_CEILING, .speed = 1},
    {.special = 7, .trigger = SECTOR_USE, .mode = SECTOR_STAIRS, .speed = 0.25f, .amount = 8},
    {.special = 8, .trigger = SECTOR_WALK, .mode = SECTOR_STAIRS, .speed = 0.25f, .amount = 8},
    {.special = 15, .trigger = SECTOR_USE, .mode = SECTOR_PLATFORM_AMOUNT, .speed = 0.5f,
     .amount = 24, .change = SECTOR_CHANGE_TEXTURE},
    {.special = 18, .trigger = SECTOR_USE, .mode = SECTOR_RAISE_NEXT, .speed = 1},
    {.special = 20, .trigger = SECTOR_USE, .mode = SECTOR_PLATFORM_NEXT, .speed = 0.5f,
     .change = SECTOR_CHANGE_ZERO},
    {.special = 22, .trigger = SECTOR_WALK, .mode = SECTOR_PLATFORM_NEXT, .speed = 0.5f,
     .change = SECTOR_CHANGE_ZERO},
    {.special = 30, .trigger = SECTOR_WALK, .mode = SECTOR_RAISE_TEXTURE, .speed = 1},
    {.special = 37, .trigger = SECTOR_WALK, .mode = SECTOR_LOWER_LOWEST, .speed = 1,
     .change = SECTOR_CHANGE_TYPE, .numeric = true},
    {.special = 47, .trigger = SECTOR_SHOOT, .mode = SECTOR_PLATFORM_NEXT, .speed = 0.5f,
     .change = SECTOR_CHANGE_ZERO},
    {.special = 49, .trigger = SECTOR_USE, .mode = SECTOR_CRUSHER, .speed = 1, .crush = true},
    {.special = 59, .trigger = SECTOR_WALK, .mode = SECTOR_RAISE_AMOUNT, .speed = 1,
     .amount = 24, .change = SECTOR_CHANGE_TYPE},
    {.special = 67, .trigger = SECTOR_USE, .mode = SECTOR_PLATFORM_AMOUNT, .repeat = true,
     .speed = 0.5f, .amount = 32, .change = SECTOR_CHANGE_ZERO},
    {.special = 68, .trigger = SECTOR_USE, .mode = SECTOR_PLATFORM_NEXT, .repeat = true,
     .speed = 0.5f, .change = SECTOR_CHANGE_ZERO},
    {.special = 73, .trigger = SECTOR_WALK, .mode = SECTOR_CRUSHER, .repeat = true,
     .speed = 1, .crush = true},
    {.special = 74, .trigger = SECTOR_WALK, .mode = SECTOR_STOP_CRUSHER, .repeat = true},
    {.special = 77, .trigger = SECTOR_WALK, .mode = SECTOR_CRUSHER, .repeat = true,
     .speed = 2, .crush = true},
    {.special = 87, .trigger = SECTOR_WALK, .mode = SECTOR_PERPETUAL, .repeat = true, .speed = 1},
    {.special = 89, .trigger = SECTOR_WALK, .mode = SECTOR_STOP_PLATFORM, .repeat = true},
    {.special = 91, .trigger = SECTOR_WALK, .mode = SECTOR_RAISE_CEILING, .repeat = true, .speed = 1},
    {.special = 94, .trigger = SECTOR_WALK, .mode = SECTOR_RAISE_CEILING, .repeat = true,
     .speed = 1, .amount = -8, .crush = true},
    {.special = 100, .trigger = SECTOR_WALK, .mode = SECTOR_STAIRS, .speed = 4,
     .amount = 16, .crush = true},
    {.special = 119, .trigger = SECTOR_WALK, .mode = SECTOR_RAISE_NEXT, .speed = 1},
    {.special = 127, .trigger = SECTOR_USE, .mode = SECTOR_STAIRS, .speed = 4,
     .amount = 16, .crush = true},
    {.special = 131, .trigger = SECTOR_USE, .mode = SECTOR_RAISE_NEXT, .speed = 4},
    {.special = 140, .trigger = SECTOR_USE, .mode = SECTOR_RAISE_AMOUNT, .speed = 1, .amount = 512},
    {.special = 141, .trigger = SECTOR_WALK, .mode = SECTOR_CRUSHER, .speed = 1,
     .crush = true}
};

SectorMovers sector_movers_create(const Scene *scene) {
    return (SectorMovers){.floors = allocate(scene->sector_count, sizeof(Floor)),
                          .ceilings = allocate(scene->sector_count, sizeof(Floor))};
}

void sector_movers_free(SectorMovers *movers) {
    free(movers->floors);
    free(movers->ceilings);
    *movers = (SectorMovers){0};
}

const SectorAction *sector_action(unsigned special) {
    for (size_t index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index)
        if (actions[index].special == special) return &actions[index];
    return NULL;
}

bool sector_movers_busy(const SectorMovers *movers, uint32_t sector) {
    return (movers->floors[sector].state != FLOOR_STOPPED || movers->ceilings[sector].state != FLOOR_STOPPED);
}

static void change_floor(Scene *scene, uint32_t sector, Floor *floor) {
    if (floor->change == SECTOR_CHANGE_NONE) return;
    scene->sectors[sector].floor_asset = floor->asset;
    if (floor->change == SECTOR_CHANGE_ZERO) scene->sectors[sector].special = 0;
    else if (floor->change == SECTOR_CHANGE_TYPE) scene->sectors[sector].special = floor->special;
    for (size_t index = 0; index < scene->batch_count; ++index) {
        Batch *batch = &scene->batches[index];
        if (batch->owner == sector && batch->plane == SURFACE_FLOOR) batch->asset = floor->asset;
    }
    floor->change = SECTOR_CHANGE_NONE;
}

static void set_model(const Scene *scene, Floor *floor, uint32_t model, unsigned change) {
    if (model >= scene->sector_count) return;
    floor->change = change;
    floor->asset = scene->sectors[model].floor_asset;
    floor->special = scene->sectors[model].special;
}

static Floor rising_floor(float bottom, float top, const SectorAction *action) {
    return (Floor){.bottom = bottom, .top = top, .speed = action->speed / 32,
                   .state = FLOOR_RISING, .mode = action->mode, .crush = action->crush};
}

static bool start_stairs(Scene *scene, SectorMovers *movers, uint32_t sector, const SectorAction *action) {
    uint32_t asset = scene->sectors[sector].floor_asset;
    float top = scene->sectors[sector].floor + action->amount / 32;
    for (;;) {
        movers->floors[sector] = rising_floor(scene->sectors[sector].floor, top, action);
        uint32_t next = UINT32_MAX;
        for (size_t index = 0; index < scene->line_count; ++index) {
            const CollisionLine *line = &scene->lines[index];
            if (line->front != sector) continue;
            uint32_t other = line_neighbor(line, sector);
            if (other == UINT32_MAX || scene->sectors[other].floor_asset != asset) continue;
            top += action->amount / 32;
            if (sector_movers_busy(movers, other)) continue;
            next = other;
            break;
        }
        if (next == UINT32_MAX) break;
        sector = next;
    }
    return true;
}

static bool pause_mover(Floor *floor) {
    if (floor->state == FLOOR_STOPPED || floor->state == FLOOR_PAUSED) return false;
    floor->saved_state = floor->state;
    floor->state = FLOOR_PAUSED;
    return true;
}

bool sector_movers_start(Scene *scene, SectorMovers *movers, uint32_t sector,
                         const SectorAction *action, const CollisionLine *trigger) {
    if (!action || sector >= scene->sector_count) return false;
    if (!trigger || !trigger->tag || scene->sectors[sector].tag != trigger->tag) return false;
    Floor *floor = &movers->floors[sector];
    Floor *ceiling = &movers->ceilings[sector];
    if (action->mode == SECTOR_STOP_PLATFORM) return floor->lift && pause_mover(floor);
    if (action->mode == SECTOR_STOP_CRUSHER) return ceiling->mode == SECTOR_CRUSHER && pause_mover(ceiling);
    Floor *resume = action->mode == SECTOR_CRUSHER ? ceiling : floor;
    if ((action->mode == SECTOR_CRUSHER || action->mode == SECTOR_PERPETUAL) &&
        resume->state == FLOOR_PAUSED && (resume->mode == SECTOR_CRUSHER || resume->lift)) {
        resume->state = resume->saved_state;
        return true;
    }
    if (sector_movers_busy(movers, sector)) return false;
    if (action->mode == SECTOR_STAIRS) return start_stairs(scene, movers, sector, action);
    SceneSector *owner = &scene->sectors[sector];
    if (action->mode == SECTOR_CRUSHER) {
        *ceiling = (Floor){.bottom = owner->floor + 8.0f / 32, .top = owner->ceiling,
                           .speed = action->speed / 32, .state = FLOOR_LOWERING,
                           .mode = SECTOR_CRUSHER, .crush = true};
        return true;
    }
    float lowest = owner->floor, highest = -INFINITY, next = INFINITY, ceiling_target = owner->ceiling;
    for (size_t index = 0; index < scene->line_count; ++index) {
        uint32_t other = line_neighbor(&scene->lines[index], sector);
        if (other == UINT32_MAX) continue;
        float height = scene->sectors[other].floor;
        lowest = fminf(lowest, height);
        highest = fmaxf(highest, height);
        if (height > owner->floor) next = fminf(next, height);
        ceiling_target = fminf(ceiling_target, scene->sectors[other].ceiling);
    }
    float target = owner->floor;
    bool lowering = false;
    switch (action->mode) {
        case SECTOR_LOWER_HIGHEST:
        case SECTOR_LOWER_TURBO:
            if (!isfinite(highest)) return false;
            target = highest;
            if (action->mode == SECTOR_LOWER_TURBO && target != owner->floor) target += 8.0f / 32;
            lowering = true;
            break;
        case SECTOR_LOWER_LOWEST:
        case SECTOR_LIFT_RETURN:
        case SECTOR_PERPETUAL:
            target = lowest;
            lowering = true;
            break;
        case SECTOR_RAISE_NEXT:
        case SECTOR_PLATFORM_NEXT:
            target = isfinite(next) ? next : owner->floor;
            break;
        case SECTOR_RAISE_CEILING:
            target = ceiling_target + action->amount / 32;
            break;
        case SECTOR_RAISE_AMOUNT:
        case SECTOR_PLATFORM_AMOUNT:
            target += action->amount / 32;
            break;
        case SECTOR_RAISE_TEXTURE: {
            float height = INFINITY;
            for (size_t index = 0; index < scene->wall_count; ++index) {
                const WallSurface *wall = &scene->walls[index];
                if (wall->kind != WALL_LOWER || wall->line >= scene->line_count ||
                    wall->batch >= scene->batch_count) continue;
                if (line_neighbor(&scene->lines[wall->line], sector) == UINT32_MAX) continue;
                uint32_t asset = scene->batches[wall->batch].asset;
                if (asset < scene->asset_count) height = fminf(height, (float)scene->assets[asset].height / 32);
            }
            if (!isfinite(height)) return false;
            target += height;
            break;
        }
        default: return false;
    }
    if (lowering && !isfinite(highest)) return false;
    *floor = rising_floor(owner->floor, target, action);
    if (lowering) {
        floor->bottom = target;
        floor->top = owner->floor;
        floor->state = FLOOR_LOWERING;
    }
    floor->lift = action->mode == SECTOR_LIFT_RETURN || action->mode == SECTOR_PERPETUAL ||
                  action->mode == SECTOR_PLATFORM_NEXT || action->mode == SECTOR_PLATFORM_AMOUNT;
    if (action->mode == SECTOR_PERPETUAL) floor->top = fmaxf(owner->floor, highest);
    if (action->numeric) {
        for (size_t index = 0; index < scene->line_count; ++index) {
            uint32_t other = line_neighbor(&scene->lines[index], sector);
            if (other != UINT32_MAX && scene->sectors[other].floor == target) {
                set_model(scene, floor, other, action->change);
                break;
            }
        }
    } else {
        set_model(scene, floor, trigger->front, action->change);
        change_floor(scene, sector, floor);
    }
    return true;
}

static void crush_player(const Floor *floor, Player *player) {
    if (floor->crush && player && player->health && player->ticks % 4 == 0)
        player->health = player->health > 10 ? player->health - 10 : 0;
}

static void tick_floor(Scene *scene, Floor *floor, uint32_t sector, Player *player) {
    if (floor->state == FLOOR_STOPPED || floor->state == FLOOR_PAUSED) return;
    if (floor->state == FLOOR_WAITING) {
        if (floor->wait && --floor->wait == 0) floor->state = floor->next_state;
        return;
    }
    float previous = scene->sectors[sector].floor;
    float next = floor->state == FLOOR_LOWERING ? fmaxf(floor->bottom, previous - floor->speed) :
                                                fminf(floor->top, previous + floor->speed);
    bool touches = player_touches_sector(scene, player, sector);
    bool riding = false;
    if (touches) {
        float support, ceiling;
        player_clearance(scene, player, &support, &ceiling);
        riding = fabsf(player->feet.y - support) < 0.0001f && fabsf(support - previous) < 0.0001f;
        if (next > previous && fmaxf(next, player->feet.y) + PLAYER_HEIGHT > ceiling + 0.0001f) {
            crush_player(floor, player);
            if (floor->lift) floor->state = FLOOR_LOWERING;
            return;
        }
    }
    scene->sectors[sector].floor = next;
    if (touches && (riding || player->feet.y < next)) {
        float support, ceiling;
        player_clearance(scene, player, &support, &ceiling);
        player->feet.y = support;
        player->vertical_speed = 0;
    }
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        Sprite *sprite = &scene->sprites[index];
        if (sprite->sector == sector && !sprite->ceiling) sprite->position.y += next - previous;
    }
    bool bottom = floor->state == FLOOR_LOWERING && next <= floor->bottom;
    bool top = floor->state == FLOOR_RISING && next >= floor->top;
    if (!bottom && !top) return;
    if (floor->lift && (bottom || floor->mode == SECTOR_PERPETUAL)) {
        floor->state = FLOOR_WAITING;
        floor->wait = 105;
        floor->next_state = bottom ? FLOOR_RISING : FLOOR_LOWERING;
    } else {
        floor->state = FLOOR_STOPPED;
        change_floor(scene, sector, floor);
    }
}

static void tick_ceiling(Scene *scene, Floor *ceiling, uint32_t sector, Player *player) {
    if (ceiling->state == FLOOR_STOPPED || ceiling->state == FLOOR_PAUSED) return;
    bool lowering = ceiling->state == FLOOR_LOWERING;
    float previous = scene->sectors[sector].ceiling;
    float speed = ceiling->slowed && lowering ? ceiling->speed / 8 : ceiling->speed;
    float next = lowering ? fmaxf(ceiling->bottom, previous - speed) :
                            fminf(ceiling->top, previous + speed);
    if (lowering && player_touches_sector(scene, player, sector)) {
        float support, clearance;
        player_clearance(scene, player, &support, &clearance);
        float feet = fmaxf(player->feet.y, support);
        if (feet + PLAYER_HEIGHT > next + 0.0001f) {
            crush_player(ceiling, player);
            if (ceiling->speed < 2.0f / 32) ceiling->slowed = true;
            return;
        }
    }
    scene->sectors[sector].ceiling = next;
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        Sprite *sprite = &scene->sprites[index];
        if (sprite->sector == sector && sprite->ceiling) sprite->position.y += next - previous;
    }
    if (lowering && next <= ceiling->bottom) {
        ceiling->state = FLOOR_RISING;
        ceiling->slowed = false;
    } else if (!lowering && next >= ceiling->top) ceiling->state = FLOOR_LOWERING;
}

void sector_movers_tick(Scene *scene, SectorMovers *movers, uint32_t sector, Player *player) {
    if (sector >= scene->sector_count) return;
    tick_floor(scene, &movers->floors[sector], sector, player);
    tick_ceiling(scene, &movers->ceilings[sector], sector, player);
}
