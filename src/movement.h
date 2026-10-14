#pragma once

#include "scene.h"

static constexpr float PLAYER_RADIUS = 16.0f / 32;
static constexpr float PLAYER_HEIGHT = 56.0f / 32;
static constexpr float PLAYER_STEP = 24.0f / 32;
static constexpr float PLAYER_EYE = 41.0f / 32;

typedef struct {
    Position feet;
    float vertical_speed;
    uint32_t sector;
    float velocity_x, velocity_z, view_height, view_speed;
    uint32_t ticks;
    uint32_t keys;
} Player;

uint32_t scene_sector_at(const Scene *scene, float x, float z, uint32_t hint);
Player player_spawn(const Scene *scene);
void player_move(const Scene *scene, Player *player, float dx, float dz, float seconds);
void player_tick(const Scene *scene, Player *player, int forward, int sideways, float yaw, bool running);
float player_view_y(const Scene *scene, const Player *player);
void player_pickup_keys(Scene *scene, Player *player);
