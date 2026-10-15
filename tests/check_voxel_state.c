#include "scene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(bool condition, const char *message) {
    if (condition) return;
    fprintf(stderr, "Voxel state check failed: %s\n", message);
    exit(EXIT_FAILURE);
}

int main(void) {
    Asset assets[2] = {{.voxel = {.count = 36, .spin = 10}}, {0}};
    Sprite sprite = {.yaw = 1.5707963268f};
    Scene scene = {.assets = assets, .asset_count = 2, .sprites = &sprite, .sprite_count = 1};
    for (unsigned tick = 0; tick < 35; ++tick) scene_tick_sprites(&scene);
    expect(fabsf(sprite.yaw - 1.3962634f) < 0.00001f, "placed spin subtracts degrees per second at 35 Hz");
    float before = sprite.yaw;
    sprite.hidden = true;
    scene_tick_sprites(&scene);
    expect(sprite.yaw == before, "hidden models do not spin");
    sprite.hidden = false;
    sprite.frame_count = 2;
    sprite.frames[0] = (SpriteFrame){.asset = 0, .ticks = 1};
    sprite.frames[1] = (SpriteFrame){.asset = 1, .ticks = 1};
    expect(scene_sprite_voxel(&scene, &sprite) == NULL, "incomplete animation stays on billboards");
    assets[1].voxel = assets[0].voxel;
    expect(scene_sprite_voxel(&scene, &sprite) != NULL, "complete animation uses models");
    scene_tick_sprites(&scene);
    expect(sprite.asset == 1 && scene_sprite_voxel(&scene, &sprite) == &assets[1].voxel,
        "animation chooses its current frame mesh");
    puts("Passed voxel spin, hidden state, animation and fallback checks");
    return 0;
}
