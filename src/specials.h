#pragma once

#include "movement.h"
#include "sector_movers.h"

enum { DOOR_STOPPED, DOOR_OPENING, DOOR_WAIT_OPEN, DOOR_CLOSING, DOOR_OPEN, DOOR_WAIT_CLOSED };
enum { DOOR_RAISE, DOOR_OPEN_STAY, DOOR_CLOSE, DOOR_CLOSE_WAIT_OPEN };

typedef struct {
    float top;
    unsigned state, wait;
    float speed;
    unsigned mode;
} Door;
typedef struct { unsigned kind, minimum, maximum, wait; bool rising; } SectorLight;
typedef struct {
    Door *doors;
    SectorMovers movers;
    uint32_t *switch_ticks;
    SectorLight *lights;
    unsigned ticks, secrets, total_secrets, secret_message;
    uint32_t random;
    bool exited, secret_exit;
    float teleport_yaw;
} Specials;

Specials specials_create(const Scene *scene);
void specials_free(Specials *specials);
bool specials_use(Scene *scene, Specials *specials, const Player *player, float yaw);
bool specials_shoot(Scene *scene, Specials *specials, const Player *player, float yaw);
bool specials_cross(Scene *scene, Specials *specials, Position from, Position to, Player *player);
void specials_tick(Scene *scene, Specials *specials, Player *player);
void specials_player(Scene *scene, Specials *specials, Player *player);
bool specials_supported(unsigned special);
char *specials_next_map(const Scene *scene, const Specials *specials, const char *path);
