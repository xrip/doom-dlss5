#ifndef DOOM_FRAME_GENERATION_H
#define DOOM_FRAME_GENERATION_H

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomFrameGeneration DoomFrameGeneration;

typedef struct DoomFrameGenerationImage {
    VkImage image;
    VkImageView view;
    VkFormat format;
    VkExtent2D extent;
    VkImageLayout layout;
} DoomFrameGenerationImage;

typedef struct DoomFrameGenerationFrame {
    VkCommandBuffer command_buffer;
    DoomFrameGenerationImage backbuffer;
    DoomFrameGenerationImage interpolated;
    VkBuffer disable_interpolation;
    DoomFrameGenerationImage depth;
    DoomFrameGenerationImage motion;
    DoomFrameGenerationImage hudless;
    DoomFrameGenerationImage ui;
    float viewToClip[16];
    float clipToView[16];
    float clipToPrevClip[16];
    float prevClipToClip[16];
    float cameraPosition[3];
    float cameraForward[3];
    float cameraUp[3];
    float cameraRight[3];
    float fovy;
    float aspect;
    float nearPlane;
    float farPlane;
    float motionScale[2];
    float pixelJitter[2];
    uint32_t reset;
} DoomFrameGenerationFrame;

int doom_frame_generation_create(
    DoomFrameGeneration **out,
    char *err,
    size_t cap);

int doom_frame_generation_resize(
    DoomFrameGeneration *context,
    VkCommandBuffer command,
    VkExtent2D extent,
    VkFormat format,
    char *err,
    size_t cap);

int doom_frame_generation_evaluate(
    DoomFrameGeneration *context,
    const DoomFrameGenerationFrame *frame,
    char *err,
    size_t cap);

int doom_frame_generation_destroy(
    DoomFrameGeneration *context,
    char *err,
    size_t cap);

#ifdef __cplusplus
}
#endif

#endif
