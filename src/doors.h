#pragma once

#include "movement.h"

enum { DOOR_STOPPED, DOOR_OPENING, DOOR_WAIT_OPEN, DOOR_CLOSING, DOOR_OPEN, DOOR_WAIT_CLOSED };
enum { DOOR_RAISE, DOOR_OPEN_STAY, DOOR_CLOSE, DOOR_CLOSE_WAIT_OPEN };

typedef struct {
    float top;
    unsigned state, wait;
    float speed;
    unsigned mode;
} Door;
typedef struct { Door *sectors; uint32_t *switch_ticks; } Doors;

Doors doors_create(const Scene *scene);
void doors_free(Doors *doors);
bool doors_use(Scene *scene, Doors *doors, const Player *player, float yaw);
bool doors_shoot(Scene *scene, Doors *doors, const Player *player, float yaw);
void doors_cross(Scene *scene, Doors *doors, Position from, Position to);
void doors_tick(Scene *scene, Doors *doors, const Player *player);
