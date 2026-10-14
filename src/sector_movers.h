#pragma once

#include "movement.h"

enum { FLOOR_STOPPED, FLOOR_LOWERING, FLOOR_WAITING, FLOOR_RISING, FLOOR_PAUSED };
enum { SECTOR_USE = 0, SECTOR_WALK = 1, SECTOR_SHOOT = 2 };
enum {
    SECTOR_LOWER_HIGHEST, SECTOR_LOWER_LOWEST, SECTOR_LOWER_TURBO,
    SECTOR_LIFT_RETURN, SECTOR_RAISE_CEILING, SECTOR_RAISE_NEXT,
    SECTOR_RAISE_AMOUNT, SECTOR_RAISE_TEXTURE, SECTOR_PLATFORM_NEXT,
    SECTOR_PLATFORM_AMOUNT, SECTOR_PERPETUAL, SECTOR_STOP_PLATFORM,
    SECTOR_STAIRS, SECTOR_CRUSHER, SECTOR_STOP_CRUSHER
};
enum { SECTOR_CHANGE_NONE, SECTOR_CHANGE_TEXTURE, SECTOR_CHANGE_ZERO, SECTOR_CHANGE_TYPE };

typedef struct {
    float bottom, top, speed;
    unsigned state, wait;
    bool lift;
    unsigned mode, saved_state, next_state, change;
    uint32_t asset, special;
    bool crush, slowed;
} Floor;

typedef struct {
    Floor *floors, *ceilings;
} SectorMovers;

typedef struct {
    unsigned special, trigger, mode;
    bool repeat;
    float speed;
    float amount;
    unsigned change;
    bool numeric, crush;
} SectorAction;

SectorMovers sector_movers_create(const Scene *scene);
void sector_movers_free(SectorMovers *movers);
const SectorAction *sector_action(unsigned special);
bool sector_movers_start(Scene *scene, SectorMovers *movers, uint32_t sector,
                         const SectorAction *action, const CollisionLine *trigger);
void sector_movers_tick(Scene *scene, SectorMovers *movers, uint32_t sector, Player *player);
bool sector_movers_busy(const SectorMovers *movers, uint32_t sector);
